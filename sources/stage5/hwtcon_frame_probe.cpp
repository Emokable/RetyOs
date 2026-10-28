// Reversible, single-rectangle ColorSoft framebuffer bring-up probe.
// This utility is never installed into the Android rootfs or launched by default.
#include <linux/fb.h>
#include <linux/hwtcon_ioctl_cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
const char* kDevice = "/dev/fb0";
const char* kBackup = "/mnt/us/.kindle-a7-hwtcon-restore.bin";
const uint32_t kMagic = 0x41374857;  // "W H 7 A" in the little-endian file.
const uint32_t kX = 32, kY = 32, kW = 32, kH = 32;
const uint32_t kExpectedWidth = 1272, kExpectedHeight = 1696;
const uint32_t kExpectedVirtualHeight = 3392, kExpectedPitch = 5088;
const uint32_t kExpectedSmem = 17258496;
uint32_t gMarkerBase = 0;

struct BackupHeader {
    uint32_t magic;
    uint32_t x, y, width, height, bytes, checksum;
};

uint32_t crc32(const uint8_t* bytes, size_t length) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool writeAll(int fd, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool readAll(int fd, void* data, size_t size) {
    uint8_t* p = static_cast<uint8_t*>(data);
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

void* openFramebuffer(int* fd_out, size_t* size_out) {
    const int fd = open(kDevice, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open framebuffer"); return MAP_FAILED; }
    fb_var_screeninfo var;
    fb_fix_screeninfo fix;
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        perror("read framebuffer metadata"); close(fd); return MAP_FAILED;
    }
    if (var.xres != kExpectedWidth || var.yres != kExpectedHeight ||
        var.yres_virtual != kExpectedVirtualHeight || var.bits_per_pixel != 32 ||
        fix.line_length != kExpectedPitch || fix.smem_len != kExpectedSmem ||
        var.red.offset != 0 || var.green.offset != 8 ||
        var.blue.offset != 16 || var.transp.offset != 24) {
        fprintf(stderr, "refusing unexpected fb layout: %ux%u virt_y=%u bpp=%u pitch=%u smem=%u channels=%u/%u/%u/%u\n",
                var.xres, var.yres, var.yres_virtual, var.bits_per_pixel,
                fix.line_length, fix.smem_len, var.red.offset, var.green.offset,
                var.blue.offset, var.transp.offset);
        close(fd); return MAP_FAILED;
    }
    void* map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { perror("mmap framebuffer"); close(fd); return MAP_FAILED; }
    *fd_out = fd;
    *size_out = fix.smem_len;
    return map;
}

uint8_t* pixel(uint8_t* map, uint32_t x, uint32_t y) {
    return map + static_cast<size_t>(y) * kExpectedPitch + static_cast<size_t>(x) * 4;
}

bool saveBackup(const uint8_t* map) {
    const int fd = open(kBackup, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { perror("create recovery backup (existing backup is preserved)"); return false; }
    uint8_t pixels[kW * kH * 4];
    for (uint32_t row = 0; row < kH; ++row)
        memcpy(pixels + row * kW * 4, pixel(const_cast<uint8_t*>(map), kX, kY + row), kW * 4);
    BackupHeader h = {kMagic, kX, kY, kW, kH, sizeof(pixels), crc32(pixels, sizeof(pixels))};
    const bool ok = writeAll(fd, &h, sizeof(h)) && writeAll(fd, pixels, sizeof(pixels)) && fsync(fd) == 0;
    const int saved_errno = errno;
    close(fd);
    if (!ok) { unlink(kBackup); errno = saved_errno; perror("write recovery backup"); return false; }
    return true;
}

bool loadBackup(uint8_t* pixels) {
    const int fd = open(kBackup, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("open recovery backup"); return false; }
    struct stat st;
    BackupHeader h;
    const bool valid = fstat(fd, &st) == 0 && st.st_size == sizeof(h) + kW * kH * 4 &&
        readAll(fd, &h, sizeof(h)) && readAll(fd, pixels, kW * kH * 4) &&
        h.magic == kMagic && h.x == kX && h.y == kY && h.width == kW &&
        h.height == kH && h.bytes == kW * kH * 4 &&
        h.checksum == crc32(pixels, kW * kH * 4);
    close(fd);
    if (!valid) fprintf(stderr, "recovery backup failed size/header/checksum validation; preserved\n");
    return valid;
}

bool sendUpdate(int fd, uint32_t marker) {
    mxcfb_update_data request;
    memset(&request, 0, sizeof(request));
    request.update_region.top = kY;
    request.update_region.left = kX;
    request.update_region.width = kW;
    request.update_region.height = kH;
    request.waveform_mode = WAVEFORM_MODE_AUTO;
    request.update_mode = UPDATE_MODE_PARTIAL;
    request.update_marker = marker;
    if (ioctl(fd, MXCFB_SEND_UPDATE, &request) != 0) { perror("submit bounded HWTCON update"); return false; }
    mxcfb_update_marker_data completion;
    memset(&completion, 0, sizeof(completion));
    completion.update_marker = marker;
    if (ioctl(fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &completion) != 0) {
        perror("wait for HWTCON update completion"); return false;
    }
    return true;
}

bool restorePixels(uint8_t* map, int fd, bool remove_backup) {
    uint8_t pixels[kW * kH * 4];
    if (!loadBackup(pixels)) return false;
    for (uint32_t row = 0; row < kH; ++row)
        memcpy(pixel(map, kX, kY + row), pixels + row * kW * 4, kW * 4);
    for (uint32_t row = 0; row < kH; ++row) {
        if (memcmp(pixel(map, kX, kY + row), pixels + row * kW * 4, kW * 4) != 0) {
            fprintf(stderr, "framebuffer memory restore verification failed; backup preserved\n");
            return false;
        }
    }
    if (!sendUpdate(fd, gMarkerBase + 1U)) {
        fprintf(stderr, "screen restore update did not complete; recovery backup preserved\n");
        return false;
    }
    if (remove_backup && unlink(kBackup) != 0) { perror("remove verified recovery backup"); return false; }
    return true;
}

bool snapshotFullFrame(const uint8_t* map) {
    const int fd = open(kBackup, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { perror("create full-frame recovery backup (existing backup is preserved)"); return false; }
    // The boot progress renderer may still update the mapped framebuffer.
    // Hash and write the same immutable copy to prevent a mismatched snapshot.
    uint8_t* snapshot = static_cast<uint8_t*>(malloc(kExpectedSmem));
    if (!snapshot) { close(fd); unlink(kBackup); return false; }
    memcpy(snapshot, map, kExpectedSmem);
    BackupHeader h = {kMagic, 0, 0, kExpectedWidth, kExpectedHeight,
                      kExpectedSmem, crc32(snapshot, kExpectedSmem)};
    const bool ok = writeAll(fd, &h, sizeof(h)) && writeAll(fd, snapshot, kExpectedSmem) && fsync(fd) == 0;
    free(snapshot);
    const int saved_errno = errno;
    close(fd);
    if (!ok) { unlink(kBackup); errno = saved_errno; perror("write full-frame recovery backup"); return false; }
    return true;
}

bool restoreFullFrame(uint8_t* map, int fd) {
    uint8_t* bytes = static_cast<uint8_t*>(malloc(kExpectedSmem));
    if (!bytes) { perror("allocate full-frame recovery buffer"); return false; }
    const int backup = open(kBackup, O_RDONLY | O_CLOEXEC);
    if (backup < 0) { perror("open full-frame recovery backup"); free(bytes); return false; }
    struct stat st;
    BackupHeader h;
    const bool valid = fstat(backup, &st) == 0 && st.st_size == sizeof(h) + kExpectedSmem &&
        readAll(backup, &h, sizeof(h)) && readAll(backup, bytes, kExpectedSmem) &&
        h.magic == kMagic && h.x == 0 && h.y == 0 && h.width == kExpectedWidth &&
        h.height == kExpectedHeight && h.bytes == kExpectedSmem &&
        h.checksum == crc32(bytes, kExpectedSmem);
    close(backup);
    if (!valid) {
        fprintf(stderr, "full-frame recovery backup failed size/header/checksum validation; preserved\n");
        free(bytes);
        return false;
    }
    memcpy(map, bytes, kExpectedSmem);
    const bool memory_ok = memcmp(map, bytes, kExpectedSmem) == 0;
    free(bytes);
    if (!memory_ok) { fprintf(stderr, "full-frame memory restore verification failed; backup preserved\n"); return false; }

    const char* defer = getenv("KINDLE_RECOVERY_DEFER_REFRESH");
    if (defer && strcmp(defer, "1") == 0) {
        if (unlink(kBackup) != 0) { perror("remove verified full-frame recovery backup"); return false; }
        puts("full-frame snapshot restored; display refresh deferred to native GUI");
        return true;
    }

    mxcfb_update_data update;
    memset(&update, 0, sizeof(update));
    update.update_region.top = 0;
    update.update_region.left = 0;
    update.update_region.width = kExpectedWidth;
    update.update_region.height = kExpectedHeight;
    update.waveform_mode = WAVEFORM_MODE_AUTO;
    update.update_mode = UPDATE_MODE_PARTIAL;
    update.update_marker = gMarkerBase + 1U;
    if (ioctl(fd, MXCFB_SEND_UPDATE, &update) != 0) {
        perror("submit full-screen recovery update");
        return false;
    }
    mxcfb_update_marker_data completion;
    memset(&completion, 0, sizeof(completion));
    completion.update_marker = update.update_marker;
    if (ioctl(fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &completion) != 0) {
        perror("wait for full-screen recovery update");
        return false;
    }
    if (unlink(kBackup) != 0) { perror("remove verified full-frame recovery backup"); return false; }
    puts("full-frame snapshot restored and refreshed");
    return true;
}

int runFullFrame(bool restore_only) {
    int fd = -1;
    size_t mapped_size = 0;
    uint8_t* map = static_cast<uint8_t*>(openFramebuffer(&fd, &mapped_size));
    if (map == MAP_FAILED) return 2;
    gMarkerBase = (static_cast<uint32_t>(time(NULL)) << 8) ^ static_cast<uint32_t>(getpid());
    if (gMarkerBase == 0) gMarkerBase = 0xA7100000U;
    const bool result = restore_only ? restoreFullFrame(map, fd) : snapshotFullFrame(map);
    munmap(map, mapped_size);
    close(fd);
    if (result && !restore_only) puts("full-frame recovery snapshot ready");
    return result ? 0 : 3;
}

int run(bool restore_only) {
    int fd = -1;
    size_t mapped_size = 0;
    uint8_t* map = static_cast<uint8_t*>(openFramebuffer(&fd, &mapped_size));
    if (map == MAP_FAILED) return 2;
    bool result = false;
    if (restore_only) {
        gMarkerBase = (static_cast<uint32_t>(time(NULL)) << 8) ^ static_cast<uint32_t>(getpid());
        if (gMarkerBase == 0) gMarkerBase = 0xA7100000U;
        result = restorePixels(map, fd, true);
    } else if (saveBackup(map)) {
        gMarkerBase = (static_cast<uint32_t>(time(NULL)) << 8) ^ static_cast<uint32_t>(getpid());
        if (gMarkerBase == 0) gMarkerBase = 0xA7100000U;
        // Four unobtrusive RGBA swatches; always restore the captured 32x32 region.
        for (uint32_t y = 0; y < kH; ++y) for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = pixel(map, kX + x, kY + y);
            const bool right = x >= kW / 2, bottom = y >= kH / 2;
            p[0] = right ? 0xE0 : 0x20;
            p[1] = bottom ? 0xD0 : 0x30;
            p[2] = (!right && !bottom) ? 0xE0 : 0x40;
            p[3] = 0xFF;
        }
        if (sendUpdate(fd, gMarkerBase)) {
            result = restorePixels(map, fd, true);
        } else {
            // Queue restoration after any submitted test update; keep backup if it fails.
            fprintf(stderr, "test update failed; attempting immediate screen restoration\n");
            result = restorePixels(map, fd, false);
            if (result) {
                if (unlink(kBackup) != 0) { perror("remove verified recovery backup"); result = false; }
            } else {
                fprintf(stderr, "recovery backup preserved; run this utility with --restore\n");
            }
        }
    }
    munmap(map, mapped_size);
    close(fd);
    if (result && !restore_only) puts("bounded color update and framebuffer restore passed");
    if (result && restore_only) puts("pending framebuffer restore passed");
    return result ? 0 : 3;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--test-and-restore") == 0) return run(false);
    if (argc == 2 && strcmp(argv[1], "--restore") == 0) return run(true);
    if (argc == 2 && strcmp(argv[1], "--snapshot-full") == 0) return runFullFrame(false);
    if (argc == 2 && strcmp(argv[1], "--restore-full") == 0) return runFullFrame(true);
    fprintf(stderr, "usage: %s --test-and-restore | --restore | --snapshot-full | --restore-full\n", argv[0]);
    return 64;
}
