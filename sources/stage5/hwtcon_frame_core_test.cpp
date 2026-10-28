#include "hwtcon_frame_core.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

using kindle_a71::FrameLayout;
using kindle_a71::Rect;
using kindle_a71::UpdateRequest;

static_assert(sizeof(UpdateRequest) == 96, "HWTCON ARM32 ioctl payload must be 96 bytes");
static_assert(offsetof(UpdateRequest, region) == 0, "region ABI offset");
static_assert(offsetof(UpdateRequest, waveform_mode) == 16, "waveform ABI offset");
static_assert(offsetof(UpdateRequest, marker) == 24, "marker ABI offset");
static_assert(offsetof(UpdateRequest, alt_buffer) == 44, "alt buffer ABI offset");
static_assert(offsetof(UpdateRequest, swipe) == 72, "swipe ABI offset");
static_assert(offsetof(UpdateRequest, reserved) == 80, "tail ABI offset");

int main() {
    const FrameLayout layout = {1272, 1696, 3392, 5088, 32, 0, 8, 16, 24};
    assert(kindle_a71::ValidateColorSoftLayout(layout));
    FrameLayout bad = layout;
    bad.pitch_bytes = 5084;
    assert(!kindle_a71::ValidateColorSoftLayout(bad));

    const uint32_t source_pitch = 1272 * 4 + 16;
    const size_t source_size = static_cast<size_t>(source_pitch) * layout.height;
    const size_t destination_size = static_cast<size_t>(layout.pitch_bytes) * layout.virtual_height;
    std::vector<uint8_t> src(source_size, 0x21);
    std::vector<uint8_t> dst(destination_size, 0xA5);
    for (uint32_t y = 0; y < layout.height; ++y)
        memset(dst.data() + static_cast<size_t>(y) * layout.pitch_bytes, 0x21,
               layout.width * 4);

    Rect damage = {0, 0, 0, 0};
    assert(kindle_a71::FindDamageRgba8888(src.data(), source_pitch, dst.data(), layout, &damage));
    assert(damage.width == 0 && damage.height == 0);

    const uint32_t x0 = 11, y0 = 23, x1 = 19, y1 = 27;
    memset(src.data() + static_cast<size_t>(y0) * source_pitch + x0 * 4, 0x42, 4);
    memset(src.data() + static_cast<size_t>(y1) * source_pitch + x1 * 4, 0x64, 4);
    assert(kindle_a71::FindDamageRgba8888(src.data(), source_pitch, dst.data(), layout, &damage));
    assert(damage.left == x0 && damage.top == y0 && damage.width == x1 - x0 + 1 &&
           damage.height == y1 - y0 + 1);
    const uint8_t untouched = dst[0];
    assert(kindle_a71::CopyDamageRgba8888(src.data(), source_pitch, dst.data(), layout, damage));
    assert(memcmp(dst.data() + static_cast<size_t>(y0) * layout.pitch_bytes + x0 * 4,
                  src.data() + static_cast<size_t>(y0) * source_pitch + x0 * 4, 4) == 0);
    assert(memcmp(dst.data() + static_cast<size_t>(y1) * layout.pitch_bytes + x1 * 4,
                  src.data() + static_cast<size_t>(y1) * source_pitch + x1 * 4, 4) == 0);
    assert(dst[0] == untouched);
    assert(dst[static_cast<size_t>(layout.height) * layout.pitch_bytes] == 0xA5);
    assert(!kindle_a71::CopyDamageRgba8888(src.data(), source_pitch, dst.data(), layout,
                                            Rect{1271, 1695, 2, 1}));

    UpdateRequest request;
    assert(kindle_a71::MakePartialAutoUpdate(damage, 77, &request));
    assert(request.region.top == y0 && request.region.left == x0 &&
           request.region.width == damage.width && request.region.height == damage.height);
    assert(request.waveform_mode == 257 && request.update_mode == 0 && request.marker == 77);
    assert(kindle_a71::EnableSwipeAnimation(&request, 2, 12));
    assert(request.flags == 0x10000 && request.waveform_mode == 13 &&
           request.swipe.direction == 2 && request.swipe.steps == 12);
    assert(!kindle_a71::EnableSwipeAnimation(&request, 4, 12));
    assert(!kindle_a71::EnableSwipeAnimation(&request, 2, 61));
    assert(kindle_a71::IsLargePageChange(Rect{20, 20, 1200, 1500}));
    assert(!kindle_a71::IsLargePageChange(Rect{0, 0, 500, 700}));
    assert(!kindle_a71::IsLargePageChange(Rect{1200, 0, 100, 700}));
    assert(!kindle_a71::MakePartialAutoUpdate(damage, 0, &request));
    assert(!kindle_a71::MakePartialAutoUpdate(Rect{1272, 0, 1, 1}, 78, &request));
    // Check the optimized bounding scan against known scattered edits.
    uint32_t seed = 1234567;
    for (int trial = 0; trial < 12; ++trial) {
        for (uint32_t y = 0; y < layout.height; ++y)
            memcpy(src.data()+y*source_pitch, dst.data()+y*layout.pitch_bytes, layout.width*4);
        uint32_t left=1272, top=1696, right=0, bottom=0;
        for (int point=0; point<30; ++point) {
            seed=seed*1664525+1013904223; uint32_t x=seed%1272;
            seed=seed*1664525+1013904223; uint32_t y=seed%1696;
            src[y*source_pitch+x*4] = dst[y*layout.pitch_bytes+x*4]^0xff;
            if(x<left)left=x; if(x>right)right=x; if(y<top)top=y; if(y>bottom)bottom=y;
        }
        assert(kindle_a71::FindDamageRgba8888(src.data(),source_pitch,dst.data(),layout,&damage));
        assert(damage.left==left && damage.top==top && damage.width==right-left+1 && damage.height==bottom-top+1);
    }
    // Match the previous conversion exactly, including alpha and row padding.
    for (int mode=0; mode<2; ++mode) {
        std::vector<uint8_t> actual(256*4+16, 0xa5), expected=actual;
        for (int pattern=0; pattern<3; ++pattern) {
            for (unsigned x=0; x<256; ++x) {
                actual[x*4]=x;
                actual[x*4+1]=pattern==0?x:(x*73)%256;
                actual[x*4+2]=pattern==0?x:(x*139+pattern)%256;
            }
            expected=actual;
            for (unsigned x=0; x<256; ++x) {
                uint8_t* p=expected.data()+x*4;
                unsigned lum=(77U*p[0]+150U*p[1]+29U*p[2])>>8;
                p[0]=p[1]=p[2]=mode?(lum<160?0:255):((lum+8)/17)*17;
            }
            kindle_a71::ConvertReadingRgba8888(actual.data(),256,1,1040,mode!=0);
            assert(actual==expected);
        }
    }
    return 0;
}
