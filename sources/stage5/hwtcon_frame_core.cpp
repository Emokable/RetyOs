#include "hwtcon_frame_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

namespace kindle_a71 {
namespace {
const uint32_t kWidth = 1272;
const uint32_t kHeight = 1696;
const uint32_t kPitch = 5088;
const uint32_t kVirtualHeight = 3392;
const uint32_t kBytesPerPixel = 4;
const uint32_t kWaveformAuto = 257;
const uint32_t kUpdatePartial = 0;
const uint32_t kSwipeFlag = 0x10000;
const uint32_t kMaxSwipeSteps = 60;

bool RectWithin(const Rect& r, const FrameLayout& l) {
    return r.width > 0 && r.height > 0 && r.left < l.width && r.top < l.height &&
           r.width <= l.width - r.left && r.height <= l.height - r.top;
}
}  // namespace

bool ValidateColorSoftLayout(const FrameLayout& l) {
    return l.width == kWidth && l.height == kHeight &&
           l.virtual_height == kVirtualHeight && l.pitch_bytes == kPitch &&
           l.bits_per_pixel == 32 && l.red_offset == 0 &&
           l.green_offset == 8 && l.blue_offset == 16 && l.alpha_offset == 24 &&
           l.pitch_bytes >= l.width * kBytesPerPixel &&
           l.virtual_height >= l.height;
}

bool ValidateLayoutGray8(const FrameLayout& l) {
    // Measured on Kindle 11 (fb0-abi-probe): 1072x1448, pitch 1072,
    // virt 2896, 8bpp grayscale, hwtcon_v2 driver. Validate the generic
    // gray-8 contract instead of pinning one panel, so a firmware that
    // changes the geometry is reported by the open path, not crashed on.
    return l.width > 0 && l.height > 0 && l.bits_per_pixel == 8 &&
           l.red_offset == 0 && l.green_offset == 0 && l.blue_offset == 0 &&
           l.pitch_bytes >= l.width && l.virtual_height >= l.height;
}

bool FindDamageRgba8888(const uint8_t* src, uint32_t src_pitch,
                        const uint8_t* dst, const FrameLayout& l, Rect* damage) {
    if (!src || !dst || !damage || !ValidateColorSoftLayout(l) ||
        src_pitch < l.width * kBytesPerPixel) return false;

    uint32_t min_x = l.width, min_y = l.height, max_x = 0, max_y = 0;
    bool changed = false;
    for (uint32_t y = 0; y < l.height; ++y) {
        const uint8_t* srow = src + static_cast<size_t>(y) * src_pitch;
        const uint8_t* drow = dst + static_cast<size_t>(y) * l.pitch_bytes;
        const size_t row_bytes = static_cast<size_t>(l.width) * kBytesPerPixel;
        if (memcmp(srow, drow, row_bytes) == 0) continue;
        // Only the outermost changed pixels matter for the bounding box.
        // Dense page redraws do not need a comparison for every interior pixel.
        uint32_t first = 0, last = l.width - 1;
        while (first < l.width && memcmp(srow + first * 4, drow + first * 4, 4) == 0) ++first;
        while (last > first && memcmp(srow + last * 4, drow + last * 4, 4) == 0) --last;
        if (!changed) { min_x = first; max_x = last; min_y = max_y = y; changed = true; }
        else { if (first < min_x) min_x = first; if (last > max_x) max_x = last; max_y = y; }

    }
    if (!changed) {
        *damage = Rect{0, 0, 0, 0};
        return true;
    }
    *damage = Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
    return true;
}

bool CopyDamageRgba8888(const uint8_t* src, uint32_t src_pitch,
                        uint8_t* dst, const FrameLayout& l, const Rect& r) {
    if (!src || !dst || !ValidateColorSoftLayout(l) ||
        src_pitch < l.width * kBytesPerPixel || !RectWithin(r, l)) return false;
    const size_t bytes = static_cast<size_t>(r.width) * kBytesPerPixel;
    for (uint32_t y = r.top; y < r.top + r.height; ++y) {
        const size_t offset = static_cast<size_t>(y) * l.pitch_bytes +
                              static_cast<size_t>(r.left) * kBytesPerPixel;
        const size_t source_offset = static_cast<size_t>(y) * src_pitch +
                                     static_cast<size_t>(r.left) * kBytesPerPixel;
        memcpy(dst + offset, src + source_offset, bytes);
    }
    return true;
}

bool FindDamageGray8Rows(const uint8_t* src, uint32_t src_pitch,
                         const uint8_t* dst, const FrameLayout& l,
                         uint32_t y0, uint32_t y1, Rect* damage) {
    // The caller has already quantized the RGBA8888 source in place
    // (ConvertReadingRgba8888), so each pixel's luminance byte is channel 0.
    if (!src || !dst || !damage || !ValidateLayoutGray8(l) ||
        src_pitch < l.width * kBytesPerPixel) return false;
    if (y0 > y1 || y1 > l.height) return false;
    uint32_t min_x = l.width, min_y = l.height, max_x = 0, max_y = 0;
    bool changed = false;
    for (uint32_t y = y0; y < y1; ++y) {
        const uint8_t* srow = src + static_cast<size_t>(y) * src_pitch;
        const uint8_t* drow = dst + static_cast<size_t>(y) * l.pitch_bytes;
        uint32_t first = 0, last = l.width - 1;
        while (first < l.width &&
               drow[first] == srow[static_cast<size_t>(first) * 4]) ++first;
        if (first == l.width) continue;  // Row identical after quantization.
        while (last > first &&
               drow[last] == srow[static_cast<size_t>(last) * 4]) --last;
        if (!changed) { min_x = first; max_x = last; min_y = max_y = y; changed = true; }
        else { if (first < min_x) min_x = first; if (last > max_x) max_x = last; max_y = y; }
    }
    if (!changed) {
        *damage = Rect{0, 0, 0, 0};
        return true;
    }
    *damage = Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
    return true;
}

bool FindDamageGray8(const uint8_t* src, uint32_t src_pitch,
                     const uint8_t* dst, const FrameLayout& l, Rect* damage) {
    if (!ValidateLayoutGray8(l)) return false;
    return FindDamageGray8Rows(src, src_pitch, dst, l, 0, l.height, damage);
}

bool CopyDamageGray8(const uint8_t* src, uint32_t src_pitch,
                     uint8_t* dst, const FrameLayout& l, const Rect& r) {
    if (!src || !dst || !ValidateLayoutGray8(l) ||
        src_pitch < l.width * kBytesPerPixel || !RectWithin(r, l)) return false;
    for (uint32_t y = r.top; y < r.top + r.height; ++y) {
        const uint8_t* srow = src + static_cast<size_t>(y) * src_pitch +
                              static_cast<size_t>(r.left) * kBytesPerPixel;
        uint8_t* drow = dst + static_cast<size_t>(y) * l.pitch_bytes + r.left;
        for (uint32_t x = 0; x < r.width; ++x) drow[x] = srow[x * 4];
    }
    return true;
}

bool MakePartialAutoUpdate(const Rect& r, uint32_t marker, UpdateRequest* request) {
    if (!request || marker == 0 || r.width == 0 || r.height == 0 ||
        r.left >= kWidth || r.top >= kHeight || r.width > kWidth - r.left ||
        r.height > kHeight - r.top) return false;
    memset(request, 0, sizeof(*request));
    request->region = KernelRect{r.top, r.left, r.width, r.height};
    request->waveform_mode = kWaveformAuto;
    request->update_mode = kUpdatePartial;
    request->marker = marker;
    return true;
}

bool EnableSwipeAnimation(UpdateRequest* request, uint32_t direction,
                          uint32_t steps) {
    if (!request || direction >= 4 || steps == 0 || steps > kMaxSwipeSteps ||
        request->region.width == 0 || request->region.height == 0) return false;
    request->flags |= kSwipeFlag;
    request->waveform_mode = 13;
    request->swipe = KernelSwipe{direction, steps};
    return true;
}

void ConvertReadingRgba8888Rows(uint8_t* pixels, uint32_t width, uint32_t pitch,
                                uint32_t y0, uint32_t y1, bool monochrome) {
    if (!pixels || width > pitch / 4 || y0 >= y1) return;
    uint8_t levels[256];
    for (unsigned i=0; i<256; ++i)
        levels[i] = monochrome ? (i<160 ? 0 : 255) : ((i+8)/17)*17;
    for (uint32_t y=y0; y<y1; ++y) {
        uint8_t* row=pixels+static_cast<uint64_t>(y)*pitch;
        for (uint32_t x=0; x<width; ++x) {
            uint8_t* p=row+x*4;
            // Most reader pixels are already black/white: avoid CoW stores and
            // weighted luminance arithmetic when the RGB channels are equal.
            unsigned lum = (p[0]==p[1] && p[1]==p[2]) ? p[0] :
                (77U*p[0]+150U*p[1]+29U*p[2])>>8;
            uint8_t value=levels[lum];
            if (p[0]!=value || p[1]!=value || p[2]!=value)
                p[0]=p[1]=p[2]=value;
        }
    }
}

void ConvertReadingRgba8888(uint8_t* pixels, uint32_t width, uint32_t height,
                            uint32_t pitch, bool monochrome) {
    ConvertReadingRgba8888Rows(pixels, width, pitch, 0, height, monochrome);
}

bool IsLargePageChange(const Rect& damage) {
    FrameLayout colorsoft{};
    colorsoft.width = kWidth;
    colorsoft.height = kHeight;
    return IsLargePageChangeFor(colorsoft, damage);
}

bool IsLargePageChangeFor(const FrameLayout& l, const Rect& damage) {
    if (l.width == 0 || l.height == 0) return false;
    if (damage.width == 0 || damage.height == 0 || damage.left >= l.width ||
        damage.top >= l.height || damage.width > l.width - damage.left ||
        damage.height > l.height - damage.top) return false;
    const uint64_t changedArea = static_cast<uint64_t>(damage.width) * damage.height;
    const uint64_t screenArea = static_cast<uint64_t>(l.width) * l.height;
    return changedArea * 100 >= screenArea * 70;
}

}  // namespace kindle_a71

