#define _GNU_SOURCE
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
 struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
 if(fd<0||ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f))return 2;
 if(v.bits_per_pixel!=32||!v.xres||!v.yres||v.xres>8192||v.yres>8192||
    (uint64_t)(v.yoffset+v.yres)*f.line_length>f.smem_len)return 3;
 unsigned char *m=mmap(0,f.smem_len,PROT_READ,MAP_SHARED,fd,0);
 if(m==MAP_FAILED)return 4;
 unsigned lo=255,hi=0,n=0,dark=0,light=0; uint32_t hash=2166136261u;
 for(unsigned y=0;y<v.yres;y+=8)for(unsigned x=0;x<v.xres;x+=8){
  uint32_t p;memcpy(&p,m+(y+v.yoffset)*f.line_length+(x+v.xoffset)*4,4);
  unsigned r=(p>>v.red.offset)&255,g=(p>>v.green.offset)&255,b=(p>>v.blue.offset)&255;
  unsigned l=(r*77+g*150+b*29)>>8; if(l<lo)lo=l;if(l>hi)hi=l;
  dark+=l<100;light+=l>180;n++;hash=(hash^(p&0xffffffu))*16777619u;
 }
 munmap(m,f.smem_len);close(fd);
 int ready=hi>lo+32&&dark>8&&light>8;
 printf("FRAME_CONTENT_READY=%d width=%u height=%u min=%u max=%u dark=%u light=%u samples=%u hash=%08x\n",ready,v.xres,v.yres,lo,hi,dark,light,n,hash);
 return ready?0:1;
}
