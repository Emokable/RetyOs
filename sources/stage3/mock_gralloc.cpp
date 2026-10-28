// Isolated-only AOSP 7 gralloc/framebuffer HAL for headless SurfaceFlinger experiments.
// No physical display node, framebuffer ioctl, or host display API is opened.
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hardware/fb.h>
#include <cutils/native_handle.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <thread>
#include <chrono>
#ifdef KINDLE_HWTCON_BACKEND
#include <linux/fb.h>
#include <linux/hwtcon_ioctl_cmd.h>
#include "hwtcon_frame_core.h"
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace {
constexpr int kHandleInts = 5;
constexpr uint64_t kMaxBufferBytes = 16ULL * 1024 * 1024;
// Two fullscreen activities plus compositor targets exceed 64 MiB at 1272x1696.
constexpr uint64_t kMaxTotalBytes = 128ULL * 1024 * 1024;
#ifdef KINDLE_HWTCON_BACKEND
constexpr uint32_t kMockWidth = 1272;
constexpr uint32_t kMockHeight = 1696;
#else
constexpr uint32_t kMockWidth = 640;
constexpr uint32_t kMockHeight = 480;
#endif
std::mutex gLock;
uint64_t gAllocatedBytes = 0;
std::unordered_map<const native_handle_t*, void*> gMappings;
#ifdef KINDLE_HWTCON_BACKEND
std::mutex gHwtconLock;
std::mutex gHwtconWaitLock;
int gHwtconFd = -1;
uint8_t* gHwtconMap = nullptr;
uint8_t* gHwtconShadow = nullptr;
size_t gHwtconMapBytes = 0;
kindle_a71::FrameLayout gHwtconLayout = {};
uint32_t gUpdateMarker = 1;
uint64_t gHwtconPostCount = 0;
uint32_t gLastWaitMarker = 0;
uint64_t gLastWaitSubmittedUs = 0;
uint64_t gLastWaitTraceId = 0;
bool gScrollWasFast = false;
static uint64_t monotonicUs();
static int waitForPanel(uint32_t marker,uint64_t traceId,uint64_t submittedUs,const char* reason);
void stopAsyncDisplay();
int postAsyncDisplay(buffer_handle_t handle);
static_assert(sizeof(mxcfb_update_data) == 96, "ColorSoft HWTCON ioctl ABI mismatch");
static_assert(offsetof(mxcfb_update_data, flags) == 32, "ColorSoft HWTCON flags ABI mismatch");
static_assert(offsetof(mxcfb_update_data, swipe_data) == 72, "ColorSoft HWTCON swipe ABI mismatch");
#endif

int bytesPerPixel(int format) {
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888: return 4;
        case HAL_PIXEL_FORMAT_RGB_565: return 2;
        default: return 0;
    }
}

int createMemfd() {
#ifdef __NR_memfd_create
    return static_cast<int>(syscall(__NR_memfd_create, "sf-headless-gralloc", MFD_CLOEXEC));
#else
    errno = ENOSYS;
    return -1;
#endif
}

int moduleOpen(const hw_module_t* module, const char* name, hw_device_t** out);
int allocDeviceCloseImpl(hw_device_t* common);
int fbDeviceClose(hw_device_t* device) {
    (void)device;
#ifdef KINDLE_HWTCON_BACKEND
    stopAsyncDisplay();
    std::lock_guard<std::mutex> guard(gHwtconLock);
    if (gHwtconFd >= 0 && gLastWaitMarker != 0) {
        const uint32_t marker=gLastWaitMarker;
        const uint64_t traceId=gLastWaitTraceId;
        const uint64_t submittedUs=gLastWaitSubmittedUs;
        if (waitForPanel(marker,traceId,submittedUs,"close") != 0)
            fprintf(stderr, "HWTCON_DRAIN_ON_CLOSE_FAILED errno=%d\n", errno);
        gLastWaitMarker = 0;
    }
    free(gHwtconShadow);gHwtconShadow=nullptr;
    if (gHwtconMap) { munmap(gHwtconMap, gHwtconMapBytes); gHwtconMap = nullptr; }
    if (gHwtconFd >= 0) { close(gHwtconFd); gHwtconFd = -1; }
    gHwtconMapBytes = 0;
#endif
    return 0;
}
int fbSwapInterval(framebuffer_device_t*, int interval) { return (interval < 0 || interval > 1) ? -EINVAL : 0; }
int fbUpdateRect(framebuffer_device_t*, int left, int top, int width, int height) {
    return (left < 0 || top < 0 || width <= 0 || height <= 0) ? -EINVAL : 0;
}
int fbPost(framebuffer_device_t*, buffer_handle_t);
int fbCompositionComplete(framebuffer_device_t*) { return 0; }
void fbDump(framebuffer_device_t*, char* out, int size) {
    if (out && size > 0) out[0] = '\0';
}
int fbEnableScreen(framebuffer_device_t*, int enable) { return (enable == 0 || enable == 1) ? 0 : -EINVAL; }

#ifdef KINDLE_HWTCON_BACKEND
int ensureHwtconOpen() {
    if (gHwtconFd >= 0) return 0;
    const int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return -errno;
    fb_var_screeninfo var = {};
    fb_fix_screeninfo fix = {};
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        const int error = errno;
        close(fd);
        return -error;
    }
    if (var.xres != 1272 || var.yres != 1696 || var.yres_virtual != 3392 ||
        var.bits_per_pixel != 32 || fix.line_length != 5088 || fix.smem_len != 17258496 ||
        var.red.offset != 0 || var.green.offset != 8 || var.blue.offset != 16 ||
        var.transp.offset != 24 || strncmp(fix.id, "hwtcon_v2", sizeof(fix.id)) != 0) {
        close(fd);
        return -ENODEV;
    }
    void* map = mmap(nullptr, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        const int error = errno;
        close(fd);
        return -error;
    }
    gHwtconFd = fd;
    gHwtconMap = static_cast<uint8_t*>(map);
    gHwtconMapBytes = fix.smem_len;
    gHwtconLayout = kindle_a71::FrameLayout{var.xres, var.yres, var.yres_virtual,
        fix.line_length, var.bits_per_pixel, var.red.offset, var.green.offset,
        var.blue.offset, var.transp.offset};
    if (!kindle_a71::ValidateColorSoftLayout(gHwtconLayout)) {
        munmap(gHwtconMap, gHwtconMapBytes);
        close(gHwtconFd);
        gHwtconMap = nullptr;
        gHwtconFd = -1;
        gHwtconMapBytes = 0;
        return -ENODEV;
    }
    fprintf(stderr, "HWTCON_OPEN_OK width=%u height=%u pitch=%u bytes=%zu\n",
            var.xres, var.yres, fix.line_length, gHwtconMapBytes);
    return 0;
}

static uint64_t monotonicUs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return uint64_t(t.tv_sec)*1000000+uint64_t(t.tv_nsec)/1000; }
static int waitForPanel(uint32_t marker,uint64_t traceId,uint64_t submittedUs,const char* reason) {
    mxcfb_update_marker_data data={};data.update_marker=marker;
    const uint64_t begin=monotonicUs();
    const int result=ioctl(gHwtconFd,MXCFB_WAIT_FOR_UPDATE_COMPLETE,&data);
    const int savedErrno=errno;const uint64_t end=monotonicUs();
    // This wait may start on the next frame, long after the panel completed.
    // MTK's kernel also returns zero on its timeout path. Record actual blocking
    // separately from the age of the submitted marker; neither is an IRQ time.
    fprintf(stderr,"KINDLE_LATENCY_TRACE event=panel_wait_return trace_id=%s%llu marker=%u monotonic_ns=%llu wait_begin_ns=%llu wait_us=%llu submit_age_at_wait_us=%llu elapsed_since_submit_us=%llu ioctl_rc=%d ioctl_errno=%d completion_verified=0 reason=%s\n",
        traceId?"touch-":"none-",(unsigned long long)traceId,marker,(unsigned long long)(end*1000),
        (unsigned long long)(begin*1000),(unsigned long long)(end-begin),
        (unsigned long long)(begin-submittedUs),(unsigned long long)(end-submittedUs),
        result,result?savedErrno:0,reason);
    errno=savedErrno;return result;
}
int fbPostHwtcon(buffer_handle_t handle) {
    if(access("/data/local/tmp/kindle-session-locked",F_OK)==0)return 0;
    const uint64_t startedUs = monotonicUs();
    if (!handle || handle->numFds != 1 || handle->numInts != kHandleInts || handle->data[0] < 0 ||
        handle->data[1] != static_cast<int>(kMockWidth) ||
        handle->data[2] != static_cast<int>(kMockHeight) || handle->data[3] < handle->data[1] ||
        handle->data[4] != HAL_PIXEL_FORMAT_RGBA_8888 || handle->data[5] <= 0) return -EINVAL;
    const uint64_t sourcePitch = static_cast<uint64_t>(handle->data[3]) * 4;
    const uint64_t requiredBytes = sourcePitch * static_cast<uint64_t>(handle->data[2]);
    if (requiredBytes > static_cast<uint64_t>(handle->data[5]) || requiredBytes > kMaxBufferBytes)
        return -EINVAL;
    struct stat st;
    if (fstat(handle->data[0], &st) != 0 || st.st_size < static_cast<off_t>(requiredBytes)) return -EINVAL;
    void* sourceMap = mmap(nullptr, static_cast<size_t>(requiredBytes), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE, handle->data[0], 0);
    if (sourceMap == MAP_FAILED) return -errno;
    std::lock_guard<std::mutex> guard(gHwtconLock);
    int status = ensureHwtconOpen();
    if (status != 0) { munmap(sourceMap, static_cast<size_t>(requiredBytes)); return status; }
    const bool androidSession = getenv("KINDLE_ANDROID_SESSION") &&
            strcmp(getenv("KINDLE_ANDROID_SESSION"), "1") == 0;
    // Android exclusively owns the display. Keep comparisons in cached RAM
    // instead of repeatedly reading the device mapping; retain the safe fallback.
    if(androidSession && !gHwtconShadow){
        const size_t bytes=size_t(gHwtconLayout.pitch_bytes)*gHwtconLayout.height;
        gHwtconShadow=static_cast<uint8_t*>(malloc(bytes));
        if(gHwtconShadow){memcpy(gHwtconShadow,gHwtconMap,bytes);
            fprintf(stderr,"HWTCON_RAM_SHADOW_READY bytes=%zu\n",bytes);}
    }
    if (androidSession && gHwtconPostCount == 0) {
        const uint8_t* pixels = static_cast<const uint8_t*>(sourceMap);
        bool content = false;
        for (uint32_t y = 0; y < kMockHeight && !content; ++y) {
            for (uint32_t x = 0; x < kMockWidth; ++x) {
                const uint8_t* pixel = pixels + y * sourcePitch + x * 4;
                if (pixel[0] > 8 || pixel[1] > 8 || pixel[2] > 8) { content = true; break; }
            }
        }
        if (!content) {
            munmap(sourceMap, static_cast<size_t>(requiredBytes));
            fprintf(stderr, "HWTCON_BOOT_BLACK_FRAME_SKIPPED=1\n");
            return 0;
        }
    }
    char mode[16] = {};
    int modeFd = open("/data/local/tmp/kindle-display-mode", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (modeFd >= 0) { read(modeFd, mode, sizeof(mode) - 1); close(modeFd); }
    unsigned gestureDirection=2;unsigned long long gestureToken=0;
    int gestureEnded=0,gestureKind=2;
    FILE* gestureState=fopen("/data/local/tmp/kindle-page-direction","r");
    if(gestureState){fscanf(gestureState,"%u %llu %d %d",&gestureDirection,&gestureToken,&gestureEnded,&gestureKind);fclose(gestureState);}
    const unsigned long long gestureNow=monotonicUs()/1000;
    const bool scrollFast=androidSession && gestureKind==2 && !gestureEnded && gestureToken>0 &&
            gestureNow>=gestureToken && gestureNow-gestureToken<5000;
    if(scrollFast!=gScrollWasFast)fprintf(stderr,"HWTCON_SCROLL_FAST=%d settle_restores_color=1\n",scrollFast?1:0);
    gScrollWasFast=scrollFast;
    const bool grayscale = !strcmp(mode, "reading");
    const bool monochrome = !strcmp(mode, "fast") || scrollFast;
    char animation[8] = {};
    int animationFd = open("/data/local/tmp/kindle-display-animation", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (animationFd >= 0) { read(animationFd, animation, sizeof(animation) - 1); close(animationFd); }
    const bool animationEnabled = !strcmp(animation, "1") || !strcmp(animation, "1\n");
    unsigned animationSteps = 12;
    char stepsText[8] = {};
    int stepsFd = open("/data/local/tmp/kindle-display-animation-steps", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (stepsFd >= 0) { read(stepsFd, stepsText, sizeof(stepsText) - 1); close(stepsFd); }
    char* stepsEnd = nullptr;
    unsigned long requestedSteps = strtoul(stepsText, &stepsEnd, 10);
    if (stepsEnd != stepsText && (*stepsEnd == '\0' || *stepsEnd == '\n') &&
            (requestedSteps == 6 || requestedSteps == 12 || requestedSteps == 18))
        animationSteps = static_cast<unsigned>(requestedSteps);
    if (grayscale || monochrome)
        kindle_a71::ConvertReadingRgba8888(static_cast<uint8_t*>(sourceMap),
                kMockWidth, kMockHeight, static_cast<uint32_t>(sourcePitch), monochrome);
    static int previousMode=-1;int selectedMode=monochrome?2:grayscale?1:0;
    if(previousMode!=selectedMode){fprintf(stderr,"HWTCON_DISPLAY_MODE=%d pixels=%s\n",selectedMode,monochrome?"black-white":grayscale?"gray16":"color");previousMode=selectedMode;}
    const uint64_t compareBeginUs=monotonicUs();
    kindle_a71::Rect damage = {};
    if (!kindle_a71::FindDamageRgba8888(static_cast<const uint8_t*>(sourceMap),
            static_cast<uint32_t>(sourcePitch), gHwtconShadow?gHwtconShadow:gHwtconMap, gHwtconLayout, &damage)) {
        munmap(sourceMap, static_cast<size_t>(requiredBytes));
        return -EINVAL;
    }
    const uint64_t compareEndUs=monotonicUs();
    static unsigned long long lastAnimatedGesture=0;
    static uint64_t lastLoggedTraceId=0;
    const bool manualFull = access("/data/local/tmp/kindle-display-full-refresh", F_OK) == 0;
    // A completed gesture must not animate an unrelated screen minutes later,
    // for example the first new frame after idle lock or slow app startup.
    const bool gestureFresh = gestureToken > 0 && gestureNow >= gestureToken &&
            gestureNow - gestureToken <= 5000;
    // MotionEvent downTime and SystemClock.uptimeMillis share this millisecond
    // clock; older gestures must not be attributed to a later frame.
    const uint64_t traceId=gestureFresh?gestureToken:0;
    const bool hardwareSwipe = androidSession && animationEnabled && !manualFull && gestureFresh &&
            gestureEnded==1 && gestureKind!=2 && gestureToken>lastAnimatedGesture &&
            (gestureDirection==2||gestureDirection==3) && kindle_a71::IsLargePageChange(damage);
    if (manualFull || hardwareSwipe) damage = kindle_a71::Rect{0, 0, kMockWidth, kMockHeight};
    if (damage.width == 0 || damage.height == 0) {
        munmap(sourceMap, static_cast<size_t>(requiredBytes));
        return 0;
    }
    // CPU conversion/comparison above touches only the owned source and RAM
    // mirror. Overlap it with the prior panel update, then protect the actual
    // framebuffer write and consume its completion marker before reuse.
    const uint64_t waitBeginUs=monotonicUs();
    if(gLastWaitMarker){
        const uint32_t completedMarker=gLastWaitMarker;
        const uint64_t completedTraceId=gLastWaitTraceId;
        const uint64_t completedSubmitUs=gLastWaitSubmittedUs;
        if(waitForPanel(completedMarker,completedTraceId,completedSubmitUs,"before_reuse")!=0){
            munmap(sourceMap,static_cast<size_t>(requiredBytes));return -errno;
        }
        gLastWaitMarker=0;
    }
    const uint64_t waitEndUs=monotonicUs();
    if (!kindle_a71::CopyDamageRgba8888(static_cast<const uint8_t*>(sourceMap),
            static_cast<uint32_t>(sourcePitch), gHwtconMap, gHwtconLayout, damage)) {
        munmap(sourceMap, static_cast<size_t>(requiredBytes));
        return -EINVAL;
    }
    if(gHwtconShadow)kindle_a71::CopyDamageRgba8888(static_cast<const uint8_t*>(sourceMap),
            static_cast<uint32_t>(sourcePitch),gHwtconShadow,gHwtconLayout,damage);
    munmap(sourceMap, static_cast<size_t>(requiredBytes));
    const uint64_t frameReadyUs=monotonicUs();
    if(traceId)
        fprintf(stderr,"KINDLE_LATENCY_TRACE event=frame_generated trace_id=touch-%llu monotonic_ns=%llu input_to_frame_generated_ms=%lld origin=framebuffer_pixels_ready\n",
            (unsigned long long)traceId,(unsigned long long)(frameReadyUs*1000),
            (long long)(frameReadyUs/1000-traceId));
    mxcfb_update_data update = {};
    update.update_region.top = damage.top;
    update.update_region.left = damage.left;
    update.update_region.width = damage.width;
    update.update_region.height = damage.height;
    update.waveform_mode = WAVEFORM_MODE_AUTO;
    update.update_mode = UPDATE_MODE_PARTIAL;
    if (monochrome) update.waveform_mode = WAVEFORM_MODE_DU;
    else if (grayscale) update.waveform_mode = WAVEFORM_MODE_GL16;
    if (manualFull) { update.update_mode = UPDATE_MODE_FULL; update.waveform_mode = WAVEFORM_MODE_AUTO; }
    if (hardwareSwipe) {
        kindle_a71::UpdateRequest animationRequest = {};
        animationRequest.region = kindle_a71::KernelRect{damage.top, damage.left,
                damage.width, damage.height};
        // The kernel maps user directions through panel rotation. The previous
        // forward is 2; backward is 3, based on on-device user verification.
        unsigned direction=gestureDirection;
        fprintf(stderr,"HWTCON_DIRECTION selected=%u gesture=%llu kind=%d once_per_gesture=1\n",direction,gestureToken,gestureKind);
        // Default to the Kindle-native 12-step waveform. Explicit user choices
        // are bounded to values accepted by the ColorSoft kernel.
        if (kindle_a71::EnableSwipeAnimation(&animationRequest, direction, animationSteps)) {
            update.flags = animationRequest.flags;
            update.waveform_mode = animationRequest.waveform_mode;
            update.swipe_data.direction = animationRequest.swipe.direction;
            update.swipe_data.steps = animationRequest.swipe.steps;
            lastAnimatedGesture=gestureToken;
        }
    }
    const char* fullRefresh = getenv("KINDLE_HWTCON_FULL_REFRESH");
    const bool fullScreenDamage = damage.left == 0 && damage.top == 0 &&
            damage.width == kMockWidth && damage.height == kMockHeight;
    if (!androidSession && fullRefresh && strcmp(fullRefresh, "1") == 0 && fullScreenDamage) {
        /* AUTO selects the panel's full GC16 waveform for a full update. This
         * removes e-ink ghosting after replacing the native Kindle screen. */
        update.update_mode = UPDATE_MODE_FULL;
    }
    update.update_marker = gUpdateMarker++;
    if (gUpdateMarker == 0) gUpdateMarker = 1;
    mxcfb_update_marker_data completion = {};
    completion.update_marker = update.update_marker;
    /* Submission wait protects framebuffer reuse; it can block SurfaceFlinger.
     * Full refresh also drains physical completion before returning. */
    if (ioctl(gHwtconFd, MXCFB_SEND_UPDATE, &update) != 0) return -errno;
    const uint64_t sentUs = monotonicUs();
    uint32_t submittedMarker = update.update_marker;
    if (ioctl(gHwtconFd, MXCFB_WAIT_FOR_UPDATE_SUBMISSION, &submittedMarker) != 0) return -errno;
    if (manualFull) unlink("/data/local/tmp/kindle-display-full-refresh");
    completion.update_marker = update.update_marker;
    if (manualFull || update.update_mode == UPDATE_MODE_FULL) {
        std::lock_guard<std::mutex> waitGuard(gHwtconWaitLock);
        if (gLastWaitMarker != 0) {
            const uint32_t completedMarker=gLastWaitMarker;
            const uint64_t completedTraceId=gLastWaitTraceId;
            const uint64_t completedSubmitUs=gLastWaitSubmittedUs;
            if (waitForPanel(completedMarker,completedTraceId,completedSubmitUs,"full_drain_previous") != 0) return -errno;
        }
        if (waitForPanel(update.update_marker,traceId,sentUs,"full_update") != 0) return -errno;
        gLastWaitMarker = 0;
    } else {
        gLastWaitMarker = update.update_marker;
        gLastWaitSubmittedUs = sentUs;
        gLastWaitTraceId = traceId;
    }
    ++gHwtconPostCount;
    const uint64_t finishedUs = monotonicUs();
    const bool firstFrameForTrace=traceId!=0&&traceId!=lastLoggedTraceId;
    if(firstFrameForTrace)lastLoggedTraceId=traceId;
    if(access("/data/local/tmp/kindle-display-profile",F_OK)==0)
        fprintf(stderr,"HWTCON_PROFILE wait_us=%llu compare_us=%llu total_us=%llu\n",
            (unsigned long long)(waitEndUs-waitBeginUs),(unsigned long long)(compareEndUs-compareBeginUs),
            (unsigned long long)(finishedUs-startedUs));
    if (gHwtconPostCount <= 5 || finishedUs-startedUs > 100000 || firstFrameForTrace) {
        fprintf(stderr,"HWTCON_TIMING marker=%u prepare_us=%llu submit_wait_us=%llu total_us=%llu\n",update.update_marker,
            (unsigned long long)(sentUs-startedUs),(unsigned long long)(finishedUs-sentUs),(unsigned long long)(finishedUs-startedUs));
        fprintf(stderr,"KINDLE_LATENCY_TRACE event=frame_submitted trace_id=%s%llu marker=%u monotonic_ns=%llu input_to_submit_ms=%lld prepare_us=%llu submit_wait_us=%llu total_us=%llu\n",
            traceId?"touch-":"none-",(unsigned long long)traceId,(unsigned long long)update.update_marker,
            (unsigned long long)(sentUs*1000),
            traceId?(long long)(sentUs/1000-traceId):-1LL,
            (unsigned long long)(sentUs-startedUs),(unsigned long long)(finishedUs-sentUs),(unsigned long long)(finishedUs-startedUs));
    }
    if(firstFrameForTrace)
        fprintf(stderr,"KINDLE_LATENCY_TRACE event=display_region trace_id=touch-%llu monotonic_ns=%llu marker=%u left=%u top=%u width=%u height=%u damage_area_percent=%llu waveform=%u update_mode=%u\n",
            (unsigned long long)traceId,(unsigned long long)(sentUs*1000),update.update_marker,
            damage.left,damage.top,damage.width,damage.height,
            (unsigned long long)(uint64_t(damage.width)*damage.height*100/(uint64_t(kMockWidth)*kMockHeight)),
            update.waveform_mode,update.update_mode);
    if (update.update_mode == UPDATE_MODE_FULL)
        fprintf(stderr, "HWTCON_FULL_REFRESH=1 marker=%u waveform=auto\n", update.update_marker);
    if (update.flags & EPDC_FLAG_ENABLE_SWIPE)
        fprintf(stderr,"KINDLE_LATENCY_TRACE event=hardware_page_animation trace_id=touch-%llu marker=%u monotonic_ns=%llu input_to_submit_ms=%lld steps=%u direction=%u\n",
                (unsigned long long)gestureToken,update.update_marker,(unsigned long long)(sentUs*1000),
                (long long)(sentUs/1000-gestureToken),update.swipe_data.steps,update.swipe_data.direction);
    if (update.flags & EPDC_FLAG_ENABLE_SWIPE)
        fprintf(stderr, "HWTCON_SWIPE_REQUEST=1 op=%llu input_down_ms=%llu submit_begin_ms=%llu submit_return_ms=%llu input_to_submit_ms=%lld marker=%u direction=%u steps=%u waveform=%u\n",
                gestureToken,
                gestureToken,
                (unsigned long long)(sentUs/1000),
                (unsigned long long)(finishedUs/1000),
                (long long)(finishedUs/1000-gestureToken),
                update.update_marker, update.swipe_data.direction,
                update.swipe_data.steps, update.waveform_mode);
    if (gHwtconPostCount <= 5 || gHwtconPostCount % 50 == 0)
        fprintf(stderr, "HWTCON_UPDATE=1 count=%llu marker=%u left=%u top=%u width=%u height=%u\n",
                static_cast<unsigned long long>(gHwtconPostCount), update.update_marker,
                damage.left, damage.top, damage.width, damage.height);
    return 0;
}

// Keep physical panel waits off SurfaceFlinger's compositor thread. Exactly two
// owned snapshots: one submitted by the worker and one replaceable pending frame.
// Producer buffers may be reused immediately after post(), so retaining the
// producer's handle alone would be unsafe.
std::mutex gQueueLock;
std::condition_variable gQueueWake;
std::thread* gDisplayWorker=nullptr;
struct Snapshot { native_handle_t* handle=nullptr; uint8_t* pixels=nullptr; };
Snapshot gSnapshots[2];
int gPendingSlot=-1,gWorkingSlot=-1,gDisplayError=0;
bool gDisplayStopping=false;
uint64_t gQueuedFrames=0,gCoalescedFrames=0;
const size_t kSnapshotBytes=size_t(kMockWidth)*kMockHeight*4;
bool gInitialFrameRequestSeen=false;
void displayWorker(){
    int lastSlot=-1;
    for(;;){
        int slot;bool hadPending;
        {std::unique_lock<std::mutex> lock(gQueueLock);
         gQueueWake.wait_for(lock,std::chrono::milliseconds(250),[]{return gDisplayStopping || gPendingSlot>=0;});
         if(gPendingSlot<0 && gDisplayStopping)break;
         hadPending=gPendingSlot>=0;slot=hadPending?gPendingSlot:lastSlot;gPendingSlot=-1;gWorkingSlot=slot;}
        if(access("/data/local/tmp/kindle-display-quiesce",F_OK)==0){
            bool ok=true;
            {std::lock_guard<std::mutex> guard(gHwtconLock);
             if(gLastWaitMarker){
                 const uint32_t marker=gLastWaitMarker;const uint64_t trace=gLastWaitTraceId;const uint64_t submitted=gLastWaitSubmittedUs;
                 if(waitForPanel(marker,trace,submitted,"quiesce"))ok=false;else gLastWaitMarker=0;}}
            if(ok){int fd=open("/data/local/tmp/kindle-display-quiesced",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);if(fd>=0){write(fd,"1\n",2);close(fd);}}
            {std::lock_guard<std::mutex> lock(gQueueLock);if(slot>=0)lastSlot=slot;gWorkingSlot=-1;}
            continue;
        }
        if(slot<0){std::lock_guard<std::mutex> lock(gQueueLock);gWorkingSlot=-1;continue;}
        // Repaint even an idle Activity after native Kindle changed fb0.
        const bool resume=access("/data/local/tmp/kindle-display-resume",F_OK)==0;
        const bool initialFrameRequest=access("/data/local/tmp/kindle-display-initial-frame-request",F_OK)==0;
        if(!resume && !initialFrameRequest && !gScrollWasFast && !hadPending){std::lock_guard<std::mutex> lock(gQueueLock);gWorkingSlot=-1;continue;}
        const int status=fbPostHwtcon(gSnapshots[slot].handle);
        if(initialFrameRequest && status==0 && !gInitialFrameRequestSeen){
            bool complete=true;
            {std::lock_guard<std::mutex> waitGuard(gHwtconWaitLock);
             if(gLastWaitMarker){
                 const uint32_t marker=gLastWaitMarker;const uint64_t trace=gLastWaitTraceId;const uint64_t submitted=gLastWaitSubmittedUs;
                 if(waitForPanel(marker,trace,submitted,"initial_frame")!=0)complete=false;
                 else gLastWaitMarker=0;}}
            if(complete){
                unlink("/data/local/tmp/kindle-display-initial-frame-request");
                int fd=open("/data/local/tmp/kindle-display-initial-frame-complete",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
                if(fd>=0){write(fd,"1\n",2);close(fd);gInitialFrameRequestSeen=true;
                    fprintf(stderr,"HWTCON_COLD_INITIAL_FRAME_COMPLETE=1\n");}
            }
        }
        if(resume && status==0 && access("/data/local/tmp/kindle-session-locked",F_OK)!=0 && access("/data/local/tmp/kindle-display-full-refresh",F_OK)!=0){
            unlink("/data/local/tmp/kindle-display-resume");
            int fd=open("/data/local/tmp/kindle-display-resumed",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);if(fd>=0){write(fd,"1\n",2);close(fd);}
            fprintf(stderr,"HWTCON_WARM_RESUME_FRAME_COMPLETE=1\n");
        }
        {std::lock_guard<std::mutex> lock(gQueueLock);
         lastSlot=slot;gWorkingSlot=-1;if(status){gDisplayError=status;
             fprintf(stderr,"HWTCON_ASYNC_ERROR=%d\n",status);}}
    }
}
void stopAsyncDisplay(){
    {std::lock_guard<std::mutex> lock(gQueueLock);gDisplayStopping=true;}
    gQueueWake.notify_one();
    if(gDisplayWorker){gDisplayWorker->join();delete gDisplayWorker;gDisplayWorker=nullptr;}
    for(auto& snapshot:gSnapshots){
        if(snapshot.pixels)munmap(snapshot.pixels,kSnapshotBytes);
        if(snapshot.handle){native_handle_close(snapshot.handle);native_handle_delete(snapshot.handle);}
        snapshot=Snapshot();
    }
    gPendingSlot=gWorkingSlot=-1;gDisplayStopping=false;gScrollWasFast=false;
}
int postAsyncDisplay(buffer_handle_t source){
    const uint64_t begin=monotonicUs();
    if(!source || source->numFds!=1 || source->numInts!=kHandleInts ||
       source->data[1]!=int(kMockWidth) || source->data[2]!=int(kMockHeight) ||
       source->data[3]<int(kMockWidth) || source->data[4]!=HAL_PIXEL_FORMAT_RGBA_8888 ||
       source->data[5]<=0 || uint64_t(source->data[3])*kMockHeight*4>uint64_t(source->data[5]) ||
       uint64_t(source->data[5])>kMaxBufferBytes)return -EINVAL;
    struct stat st;if(fstat(source->data[0],&st) || st.st_size<source->data[5])return -EINVAL;
    void* pixels=mmap(nullptr,source->data[5],PROT_READ,MAP_SHARED,source->data[0],0);
    if(pixels==MAP_FAILED)return -errno;
    std::unique_lock<std::mutex> lock(gQueueLock);
    if(gDisplayError || gDisplayStopping){munmap(pixels,source->data[5]);return gDisplayError?gDisplayError:-ENODEV;}
    if(!gDisplayWorker){
        // Allocation failure is sticky until close; never leak partially built slots.
        auto fail=[&](){for(auto& item:gSnapshots){
            if(item.pixels)munmap(item.pixels,kSnapshotBytes);
            if(item.handle){native_handle_close(item.handle);native_handle_delete(item.handle);}
            item=Snapshot();}
            munmap(pixels,source->data[5]);gDisplayError=-ENOMEM;return -ENOMEM;};
        for(auto& snapshot:gSnapshots){
            snapshot.handle=native_handle_create(1,kHandleInts);
            if(!snapshot.handle)return fail();
            snapshot.handle->data[0]=-1;
            snapshot.handle->data[0]=createMemfd();
            if(snapshot.handle->data[0]<0 || ftruncate(snapshot.handle->data[0],kSnapshotBytes)){
                return fail();}
            snapshot.handle->data[1]=kMockWidth;snapshot.handle->data[2]=kMockHeight;
            snapshot.handle->data[3]=kMockWidth;snapshot.handle->data[4]=HAL_PIXEL_FORMAT_RGBA_8888;
            snapshot.handle->data[5]=kSnapshotBytes;
            void* map=mmap(nullptr,kSnapshotBytes,PROT_READ|PROT_WRITE,MAP_SHARED,snapshot.handle->data[0],0);
            if(map==MAP_FAILED)return fail();
            snapshot.pixels=static_cast<uint8_t*>(map);
        }
        gDisplayWorker=new std::thread(displayWorker);
        fprintf(stderr,"HWTCON_ASYNC_READY snapshots=2 bytes=%zu policy=latest-pending\n",2*kSnapshotBytes);
    }
    const int slot=gPendingSlot>=0?gPendingSlot:(gWorkingSlot==0?1:0);
    if(gPendingSlot>=0)++gCoalescedFrames;
    for(uint32_t y=0;y<kMockHeight;++y)
        memcpy(gSnapshots[slot].pixels+size_t(y)*kMockWidth*4,
               static_cast<const uint8_t*>(pixels)+size_t(y)*source->data[3]*4,kMockWidth*4);
    munmap(pixels,source->data[5]);gPendingSlot=slot;++gQueuedFrames;
    lock.unlock();gQueueWake.notify_one();
    const uint64_t elapsed=monotonicUs()-begin;
    if(gQueuedFrames<=5 || gQueuedFrames%100==0)
        fprintf(stderr,"HWTCON_POST_ASYNC copy_us=%llu queued=%llu coalesced=%llu\n",
                (unsigned long long)elapsed,(unsigned long long)gQueuedFrames,(unsigned long long)gCoalescedFrames);
    return 0;
}

#endif

int fbPost(framebuffer_device_t*, buffer_handle_t handle) {
#ifdef KINDLE_HWTCON_BACKEND
    const char* sync=getenv("KINDLE_HWTCON_SYNC");
    return sync && !strcmp(sync,"1") ? fbPostHwtcon(handle) : postAsyncDisplay(handle);
#else
    (void)handle;
    return 0;
#endif
}

int registerBuffer(const gralloc_module_t*, buffer_handle_t handle) {
    if (!handle || handle->numFds != 1 || handle->numInts != kHandleInts || handle->data[0] < 0) return -EINVAL;
    struct stat st;
    if (fstat(handle->data[0], &st) != 0 || st.st_size != handle->data[5]) return -EINVAL;
    return 0;
}
int unregisterBuffer(const gralloc_module_t*, buffer_handle_t handle) {
    std::lock_guard<std::mutex> guard(gLock);
    auto it = gMappings.find(handle);
    if (it != gMappings.end()) {
        munmap(it->second, static_cast<size_t>(handle->data[5]));
        gMappings.erase(it);
    }
    return 0;
}
int lockBuffer(const gralloc_module_t*, buffer_handle_t handle, int usage,
        int left, int top, int width, int height, void** address) {
    if (!handle || !address || handle->numFds != 1 || handle->numInts != kHandleInts) return -EINVAL;
    if (left < 0 || top < 0 || width < 0 || height < 0 ||
        left + width > handle->data[1] || top + height > handle->data[2]) return -EINVAL;
    (void)usage;
    std::lock_guard<std::mutex> guard(gLock);
    auto it = gMappings.find(handle);
    if (it == gMappings.end()) {
        void* map = mmap(nullptr, static_cast<size_t>(handle->data[5]), PROT_READ | PROT_WRITE,
                         MAP_SHARED, handle->data[0], 0);
        if (map == MAP_FAILED) return -errno;
        it = gMappings.emplace(handle, map).first;
    }
    *address = it->second;
    return 0;
}
int unlockBuffer(const gralloc_module_t*, buffer_handle_t handle) {
    if (!handle || handle->numFds != 1 || handle->numInts != kHandleInts) return -EINVAL;
    // memfd memory is coherent. Keep the existing mapping until unregister/free:
    // unmapping on each unlock faults the same pages back in on every redraw.
    // This retains virtual mappings, not a second copy of pixel storage.
    return 0;
}

int allocBuffer(alloc_device_t*, int width, int height, int format, int usage,
        buffer_handle_t* outHandle, int* outStride) {
    (void)usage;
    if (!outHandle || !outStride || width <= 0 || height <= 0) return -EINVAL;
    const int bpp = bytesPerPixel(format);
    if (bpp == 0) return -EINVAL;
    const uint64_t stride = static_cast<uint64_t>(width);
    const uint64_t bytes = stride * static_cast<uint64_t>(height) * static_cast<uint64_t>(bpp);
    if (bytes == 0 || bytes > kMaxBufferBytes) return -ENOMEM;
    {
        std::lock_guard<std::mutex> guard(gLock);
        if (gAllocatedBytes + bytes > kMaxTotalBytes) return -ENOMEM;
        gAllocatedBytes += bytes;
    }
    const int fd = createMemfd();
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        const int error = errno;
        if (fd >= 0) close(fd);
        std::lock_guard<std::mutex> guard(gLock);
        gAllocatedBytes -= bytes;
        return -error;
    }
    native_handle_t* handle = native_handle_create(1, kHandleInts);
    if (!handle) {
        close(fd);
        std::lock_guard<std::mutex> guard(gLock);
        gAllocatedBytes -= bytes;
        return -ENOMEM;
    }
    handle->data[0] = fd;
    handle->data[1] = width;
    handle->data[2] = height;
    handle->data[3] = static_cast<int>(stride);
    handle->data[4] = format;
    handle->data[5] = static_cast<int>(bytes);
    *outHandle = handle;
    *outStride = static_cast<int>(stride);
    return 0;
}
int freeBuffer(alloc_device_t*, buffer_handle_t handle) {
    if (!handle || handle->numFds != 1 || handle->numInts != kHandleInts) return -EINVAL;
    const uint64_t bytes = static_cast<uint64_t>(handle->data[5]);
    unregisterBuffer(nullptr, handle);
    native_handle_close(handle);
    native_handle_delete(const_cast<native_handle_t*>(handle));
    std::lock_guard<std::mutex> guard(gLock);
    gAllocatedBytes = (bytes <= gAllocatedBytes) ? (gAllocatedBytes - bytes) : 0;
    return 0;
}
int openAllocDevice(const hw_module_t* module, hw_device_t** out) {
    auto* dev = new alloc_device_t{};
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = GRALLOC_DEVICE_API_VERSION_0_1;
    dev->common.module = const_cast<hw_module_t*>(module);
    dev->common.close = allocDeviceCloseImpl;
    dev->alloc = allocBuffer;
    dev->free = freeBuffer;
    *out = &dev->common;
    return 0;
}
void closeAllocDevice(hw_device_t* common) { delete reinterpret_cast<alloc_device_t*>(common); }
int allocDeviceCloseImpl(hw_device_t* common) {
    closeAllocDevice(common);
    return 0;
}

hw_module_methods_t gMethods = { moduleOpen };
int lockBufferAsync(const gralloc_module_t* module, buffer_handle_t handle, int usage,
        int left, int top, int width, int height, void** address, int fenceFd) {
    if (fenceFd >= 0) {
        struct pollfd pfd = { fenceFd, POLLIN, 0 };
        int result;
        do { result = poll(&pfd, 1, -1); } while (result < 0 && errno == EINTR);
        close(fenceFd);
        if (result < 0) return -errno;
    }
    return lockBuffer(module, handle, usage, left, top, width, height, address);
}
int unlockBufferAsync(const gralloc_module_t* module, buffer_handle_t handle, int* fenceFd) {
    if (!fenceFd) return -EINVAL;
    const int status = unlockBuffer(module, handle);
    *fenceFd = -1;
    return status;
}
int lockYcbcrAsync(const gralloc_module_t*, buffer_handle_t, int, int, int, int, int,
        android_ycbcr*, int fenceFd) {
    if (fenceFd >= 0) close(fenceFd);
    return -EINVAL;
}

gralloc_module_t gModule = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = GRALLOC_MODULE_API_VERSION_0_3,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = GRALLOC_HARDWARE_MODULE_ID,
#ifdef KINDLE_HWTCON_BACKEND
        .name = "Isolated ColorSoft HWTCON gralloc",
#else
        .name = "Isolated headless mock gralloc",
#endif
        .author = "Local Android 7 port experiment",
        .methods = &gMethods,
    },
    .registerBuffer = registerBuffer,
    .unregisterBuffer = unregisterBuffer,
    .lock = lockBuffer,
    .unlock = unlockBuffer,
    .lockAsync = lockBufferAsync,
    .unlockAsync = unlockBufferAsync,
    .lockAsync_ycbcr = lockYcbcrAsync,
};

framebuffer_device_t gFramebuffer = {
    .common = { .tag = HARDWARE_DEVICE_TAG, .version = 0, .module = &gModule.common, .close = fbDeviceClose },
    .flags = 0,
    .width = kMockWidth,
    .height = kMockHeight,
    .stride = static_cast<int>(kMockWidth),
    .format = HAL_PIXEL_FORMAT_RGBA_8888,
    .xdpi = 160.0f,
    .ydpi = 160.0f,
    .fps = 30.0f,
    .minSwapInterval = 0,
    .maxSwapInterval = 1,
    .numFramebuffers = 2,
    .setSwapInterval = fbSwapInterval,
    .setUpdateRect = fbUpdateRect,
    .post = fbPost,
    .compositionComplete = fbCompositionComplete,
    .dump = fbDump,
    .enableScreen = fbEnableScreen,
};

int moduleOpen(const hw_module_t* module, const char* name, hw_device_t** out) {
    if (!module || !name || !out) return -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) return openAllocDevice(module, out);
    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        gFramebuffer.common.module = const_cast<hw_module_t*>(module);
        *out = &gFramebuffer.common;
        return 0;
    }
    return -EINVAL;
}
} // namespace

extern "C" gralloc_module_t HAL_MODULE_INFO_SYM = gModule;


