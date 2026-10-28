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

void ConvertReadingRgba8888(uint8_t* pixels, uint32_t width, uint32_t height,
                            uint32_t pitch, bool monochrome) {
    if (!pixels || width > pitch / 4) return;
    uint8_t levels[256];
    for (unsigned i=0; i<256; ++i)
        levels[i] = monochrome ? (i<160 ? 0 : 255) : ((i+8)/17)*17;
    for (uint32_t y=0; y<height; ++y) {
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

bool IsLargePageChange(const Rect& damage) {
    if (damage.width == 0 || damage.height == 0 || damage.left >= kWidth ||
        damage.top >= kHeight || damage.width > kWidth - damage.left ||
        damage.height > kHeight - damage.top) return false;
    const uint64_t changedArea = static_cast<uint64_t>(damage.width) * damage.height;
    const uint64_t screenArea = static_cast<uint64_t>(kWidth) * kHeight;
    return changedArea * 100 >= screenArea * 70;
}

}  // namespace kindle_a71

