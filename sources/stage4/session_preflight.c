#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int numeric(const char *s){if(!*s)return 0;for(;*s;s++)if(*s<'0'||*s>'9')return 0;return 1;}
static long long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
int main(int argc,char **argv){
 if(argc!=2||strcmp(argv[1],"--binder-idle"))return 2;
 struct stat binder;if(stat("/dev/vndbinder",&binder)||!S_ISCHR(binder.st_mode)){perror("PREFLIGHT_BINDER_DEVICE");return 3;}
 long long start=ms();unsigned checked=0,processes=0;DIR *proc=opendir("/proc");if(!proc)return 3;
 struct dirent *p;while((p=readdir(proc))){if(!numeric(p->d_name))continue;
  char path[64];int n=snprintf(path,sizeof(path),"/proc/%s/fd",p->d_name);if(n<0||(size_t)n>=sizeof(path)){closedir(proc);return 3;}
  DIR *fds=opendir(path);if(!fds){if(errno==ENOENT||errno==ESRCH)continue;perror("PREFLIGHT_PROC_FD");closedir(proc);return 3;}
  processes++;struct dirent *fd;while((fd=readdir(fds))){if(!numeric(fd->d_name))continue;
   struct stat st;if(fstatat(dirfd(fds),fd->d_name,&st,0)){if(errno==ENOENT||errno==ESRCH||errno==EBADF)continue;perror("PREFLIGHT_FSTAT");closedir(fds);closedir(proc);return 3;}
   checked++;if(S_ISCHR(st.st_mode)&&st.st_rdev==binder.st_rdev){fprintf(stderr,"VNDBINDER_BUSY=%s/%s\n",path,fd->d_name);closedir(fds);closedir(proc);return 42;}
  }closedir(fds);
 }closedir(proc);fprintf(stderr,"ANDROID_BINDER_PREFLIGHT_MS=%lld processes=%u fds=%u idle=1\n",ms()-start,processes,checked);return 0;
}
