// Golden-reference comparison through the public GGL API; no framebuffer.
#include <pixelflinger/pixelflinger.h>
#include <private/pixelflinger/ggl_context.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
using android::context_t;
struct API{void* handle;ssize_t(*init)(GGLContext**);ssize_t(*uninit)(GGLContext*);};
static uint64_t ns(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
static API load(const char* path){API a={dlopen(path,RTLD_NOW|RTLD_LOCAL),nullptr,nullptr};if(!a.handle){fprintf(stderr,"DLOPEN=%s\n",dlerror());exit(2);}a.init=(ssize_t(*)(GGLContext**))dlsym(a.handle,"gglInit");a.uninit=(ssize_t(*)(GGLContext*))dlsym(a.handle,"gglUninit");if(!a.init||!a.uninit)exit(3);return a;}
static bool render(API& api,std::vector<uint32_t>& dst,const std::vector<uint32_t>& src,int w,int h,int stride,int mode,int seed,int repeats,double* ms){
 GGLContext* g=nullptr;if(api.init(&g)!=0||!g)exit(4);
 GGLSurface target={};target.version=sizeof(target);target.width=w;target.height=h;target.stride=stride;target.data=(GGLubyte*)dst.data();target.format=GGL_PIXEL_FORMAT_RGBA_8888;
 GGLSurface tex=target;tex.data=(GGLubyte*)src.data();tex.format=mode==3?GGL_PIXEL_FORMAT_RGBA_8888:GGL_PIXEL_FORMAT_RGBX_8888;
 g->colorBuffer(g,&target);g->activeTexture(g,0);g->bindTexture(g,&tex);g->enable(g,GGL_TEXTURE_2D);
 g->shadeModel(g,GGL_FLAT);GGLclampx color[4]={65536,65536,65536,(seed%5)*16384};g->color4xv(g,color);
 g->texEnvi(g,GGL_TEXTURE_ENV,GGL_TEXTURE_ENV_MODE,GGL_REPLACE);
 g->texParameteri(g,GGL_TEXTURE_2D,GGL_TEXTURE_WRAP_S,GGL_CLAMP);g->texParameteri(g,GGL_TEXTURE_2D,GGL_TEXTURE_WRAP_T,GGL_CLAMP);
 g->texParameteri(g,GGL_TEXTURE_2D,GGL_TEXTURE_MIN_FILTER,mode==4?GGL_LINEAR:GGL_NEAREST);g->texParameteri(g,GGL_TEXTURE_2D,GGL_TEXTURE_MAG_FILTER,mode==4?GGL_LINEAR:GGL_NEAREST);
 g->texGeni(g,GGL_S,GGL_TEXTURE_GEN_MODE,GGL_AUTOMATIC);g->texGeni(g,GGL_T,GGL_TEXTURE_GEN_MODE,GGL_AUTOMATIC);
 const int scale=mode==1?32768:mode==2?-65536:65536;
 int32_t grad[8]={mode==2?(w-1)*65536:-(seed%3)*32768,scale,mode==5?16384:0,0,-(seed%3)*65536,mode==5?32768:0,65536,0};
 g->texCoordGradScale8xv(g,0,grad);
 if((seed%4)==1){g->scissor(g,1,1,w>2?w-2:0,h>2?h-2:0);g->enable(g,GGL_SCISSOR_TEST);}
 if(mode==3){g->enable(g,GGL_BLEND);g->blendFunc(g,GGL_ONE,GGL_ONE_MINUS_SRC_ALPHA);}else g->disable(g,GGL_BLEND);
 if(mode==6)g->colorMask(g,1,0,1,1);
 uint64_t start=ns();for(int i=0;i<repeats;i++)g->recti(g,0,0,w,h);*ms=(ns()-start)/1e6;
 context_t* c=reinterpret_cast<context_t*>(g);bool jit=c->scanline_as!=nullptr;
 if(w==1272)printf("PIPELINE needs=%08x:%08x:%08x:%08x jit=%d\n",c->state.needs.n,c->state.needs.p,c->state.needs.t[0],c->state.needs.t[1],jit);
 api.uninit(g);return jit;
}
int main(int argc,char**argv){
 if(argc!=3)return 2;API old=load(argv[1]),candidate=load(argv[2]);unsigned rng=0x892374;int improved=0;
 for(int test=0;test<240;test++){
  int w=1+(test*17)%97,h=1+(test*13)%73,stride=w+7;std::vector<uint32_t> src(stride*h),a(stride*h,0xa5b6c7d8),b=a;
  for(auto& v:src){rng=rng*1664525u+1013904223u;v=rng;}
  double x,y;bool ja=render(old,a,src,w,h,stride,test%7,test,1,&x),jb=render(candidate,b,src,w,h,stride,test%7,test,1,&y);
  if(a!=b){for(size_t i=0;i<a.size();i++)if(a[i]!=b[i]){printf("PIXEL_MISMATCH test=%d mode=%d index=%zu old=%08x new=%08x\n",test,test%7,i,a[i],b[i]);break;}return 5;}
  improved+=ja&&!jb;
 }
 printf("PIXEL_TESTS_PASS=240 FASTPATH_CASES=%d\n",improved);if(!improved)return 6;
 for(int test=0;test<80;test++){
  int w=1+(test*17)%97,h=1+(test*13)%73,stride=w+7;std::vector<uint32_t> a(stride*h);
  for(auto& v:a){rng=rng*1664525u+1013904223u;v=rng;}std::vector<uint32_t> b=a;
  double x,y;render(old,a,a,w,h,stride,test%3,test,1,&x);render(candidate,b,b,w,h,stride,test%3,test,1,&y);
  if(a!=b){printf("ALIAS_PIXEL_MISMATCH test=%d\n",test);return 8;}
 }
 puts("ALIAS_TESTS_PASS=80 TOTAL_PIXEL_TESTS_PASS=320");
 int w=1272,h=1696,stride=1280;std::vector<uint32_t> src(stride*h),a(stride*h),b(stride*h);
 for(auto& v:src){rng=rng*1664525u+1013904223u;v=rng;}
 for(int round=0;round<5;round++){
  double x=0,y=0;if(round&1){render(candidate,b,src,w,h,stride,0,0,3,&y);render(old,a,src,w,h,stride,0,0,3,&x);}else{render(old,a,src,w,h,stride,0,0,3,&x);render(candidate,b,src,w,h,stride,0,0,3,&y);}
  if(a!=b)return 7;printf("PERF round=%d original_ms=%.3f candidate_ms=%.3f frames=3\n",round,x,y);
 }
 return 0;
}
