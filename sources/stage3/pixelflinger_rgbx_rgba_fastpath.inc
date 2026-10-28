// Apache-2.0. Restricted RGBX8888 -> RGBA8888 nearest/clamp SRC pipeline.
// Keep incoming primary alpha, matching the legacy generated pixel pipeline.
static void scanline_rgbx_rgba_clamp(context_t* c)
{
    int count=c->iterators.xr-c->iterators.xl;
    if(count<=0)return;
    const texture_t& tx=c->state.texture[0];
    const texture_iterators_t& ti=tx.iterators;
    uint32_t* dst=reinterpret_cast<uint32_t*>(c->state.buffers.color.data)
        +c->iterators.y*c->state.buffers.color.stride+c->iterators.xl;
    const uint32_t alpha=c->packed8888&0xff000000u;
    if(ti.dsdx==65536&&ti.dtdx==0){
        const int u=(int32_t)((uint32_t)c->iterators.xl*(uint32_t)ti.dsdx+(uint32_t)ti.ydsdy)>>16;
        int v=ti.ydtdy>>16;
        if(v<0)v=0;else if(v>=(int)tx.surface.height)v=(int)tx.surface.height-1;
        if(u>=0&&count<=(int)tx.surface.width-u){
            const uint32_t* src=reinterpret_cast<const uint32_t*>(tx.surface.data)+v*tx.surface.stride+u;
            const uintptr_t d=(uintptr_t)dst,s=(uintptr_t)src,bytes=(size_t)count*4;
            if(d+bytes<=s||s+bytes<=d){
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
                const uint32x4_t mask=vdupq_n_u32(0x00ffffffu),a=vdupq_n_u32(alpha);
                while(count>=4){
                    vst1q_u32(dst,vorrq_u32(vandq_u32(vld1q_u32(src),mask),a));
                    src+=4;dst+=4;count-=4;
                }
#endif
                while(count--)*dst++=(*src++&0x00ffffffu)|alpha;
                return;
            }
        }
    }
    // Preserve the legacy coordinate iteration, edge clamp and alias order.
    clamp_iterator source(c);
    while(count--)*dst++=(source.get_pixel32()&0x00ffffffu)|alpha;
}

