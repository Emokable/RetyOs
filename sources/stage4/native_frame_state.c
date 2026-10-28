/* Private native UI snapshot. Ownership/quiescence is enforced by Rust lease. */
#define _GNU_SOURCE
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct header { uint8_t magic[8]; uint32_t bytes, stride; struct fb_var_screeninfo v; };
static int transfer(int fd, void *p, size_t n, int writing) {
    while (n) { ssize_t k=writing?write(fd,p,n):read(fd,p,n); if(k<=0)return -1; p=(char*)p+k;n-=(size_t)k; } return 0;
}
int main(int argc,char **argv) {
    if(argc!=3 || geteuid()!=0)return 2;
    int save=!strcmp(argv[1],"save");
    if(!save && strcmp(argv[1],"restore"))return 2;
    if(strncmp(argv[2],"/dev/shm/kindle-native-ui-",26))return 2;
    int fd=open("/dev/fb0",(save?O_RDONLY:O_RDWR)|O_CLOEXEC);
    struct fb_var_screeninfo v;struct fb_fix_screeninfo f;
    if(fd<0 || ioctl(fd,FBIOGET_VSCREENINFO,&v) || ioctl(fd,FBIOGET_FSCREENINFO,&f))return 3;
    if(v.bits_per_pixel!=32 || !f.smem_len || f.smem_len>64u*1024*1024 ||
        (uint64_t)v.yres_virtual*f.line_length>f.smem_len || (uint64_t)v.xres_virtual*4>f.line_length)return 4;
    struct header h={{'K','N','F','B','V','1',0,0},f.smem_len,f.line_length,v};
    int snap=open(argv[2],save?(O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW):(O_RDONLY|O_CLOEXEC|O_NOFOLLOW),0600);
    struct stat st;if(snap<0 || fstat(snap,&st) || !S_ISREG(st.st_mode) || st.st_uid!=0 || (st.st_mode&077))return 5;
    uint8_t *pixels=malloc(f.smem_len);if(!pixels)return 6;
    if(!save) {
        if(st.st_size!=(off_t)(sizeof(h)+f.smem_len) || transfer(snap,&h,sizeof(h),0) ||
            memcmp(h.magic,"KNFBV1\0",8) || h.bytes!=f.smem_len || h.stride!=f.line_length ||
            h.v.xres!=v.xres || h.v.yres!=v.yres || h.v.xres_virtual!=v.xres_virtual || h.v.yres_virtual!=v.yres_virtual ||
            h.v.bits_per_pixel!=v.bits_per_pixel || memcmp(&h.v.red,&v.red,sizeof(v.red)) ||
            memcmp(&h.v.green,&v.green,sizeof(v.green)) || memcmp(&h.v.blue,&v.blue,sizeof(v.blue)) ||
            transfer(snap,pixels,f.smem_len,0))return 7;
    }
    void *map=mmap(NULL,f.smem_len,save?PROT_READ:(PROT_READ|PROT_WRITE),MAP_SHARED,fd,0);
    if(map==MAP_FAILED)return 8;
    if(save) {
        memcpy(pixels,map,f.smem_len);
        if(transfer(snap,&h,sizeof(h),1)||transfer(snap,pixels,f.smem_len,1))return 9;
    } else {
        memcpy(map,pixels,f.smem_len);
        if(v.xoffset!=h.v.xoffset || v.yoffset!=h.v.yoffset) {
            v.xoffset=h.v.xoffset;v.yoffset=h.v.yoffset;
            if(ioctl(fd,FBIOPAN_DISPLAY,&v))return 10;
        }
    }
    munmap(map,f.smem_len);close(snap);close(fd);free(pixels);
    printf("NATIVE_FRAME_%s bytes=%u xoffset=%u yoffset=%u\n",save?"SAVED":"RESTORED",h.bytes,h.v.xoffset,h.v.yoffset);
    return 0;
}
