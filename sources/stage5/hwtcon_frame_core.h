#ifndef KINDLE_A71_HWTCON_FRAME_CORE_H
#define KINDLE_A71_HWTCON_FRAME_CORE_H

#include <stdint.h>

namespace kindle_a71 {

struct FrameLayout {
    uint32_t width;
    uint32_t height;
    uint32_t virtual_height;
    uint32_t pitch_bytes;
    uint32_t bits_per_pixel;
    uint32_t red_offset;
    uint32_t green_offset;
    uint32_t blue_offset;
    uint32_t alpha_offset;
};

struct Rect {
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
};

struct KernelRect {
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

struct KernelSwipe {
    uint32_t direction;
    uint32_t steps;
};

struct KernelAltBuffer {
    uint32_t phys_addr;
    uint32_t width;
    uint32_t height;
    KernelRect region;
};

struct UpdateRequest {
    KernelRect region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t marker;
    int32_t temperature;
    uint32_t flags;
    int32_t dither_mode;
    int32_t quant_bits;
    KernelAltBuffer alt_buffer;
    KernelSwipe swipe;
    uint32_t reserved[4];
};

// Supports the measured ColorSoft RGBA8888 mode. This routine performs no I/O.
bool ValidateColorSoftLayout(const FrameLayout& layout);
bool FindDamageRgba8888(const uint8_t* source, uint32_t source_pitch,
                        const uint8_t* destination, const FrameLayout& layout,
                        Rect* damage);
bool CopyDamageRgba8888(const uint8_t* source, uint32_t source_pitch,
                        uint8_t* destination, const FrameLayout& layout,
                        const Rect& damage);
bool MakePartialAutoUpdate(const Rect& damage, uint32_t marker,
                           UpdateRequest* request);
bool EnableSwipeAnimation(UpdateRequest* request, uint32_t direction,
                          uint32_t steps);
void ConvertReadingRgba8888(uint8_t* pixels, uint32_t width, uint32_t height,
                            uint32_t pitch, bool monochrome);
bool IsLargePageChange(const Rect& damage);

}  // namespace kindle_a71

#endif
