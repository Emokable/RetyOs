typedef unsigned int size_t;
extern int printf(const char *,...);extern int getpid(void);extern int getuid(void);
extern unsigned int alarm(unsigned int);extern long syscall(long,...);
extern int close(int);extern int ftruncate(int,long);
extern void *mmap(void *,size_t,int,int,int,long);extern int munmap(void *,size_t);
extern int *__errno(void);
extern void __libc_init(void *,void *,int (*)(int,char **,char **),const void *);
int main(int argc,char **argv,char **env){
 alarm(4);printf("BIONIC pid=%d uid=%d Android-7.1 ARM32\n",getpid(),getuid());
 int fd=(int)syscall(385,"bionic-stage3",3);
 if(fd<0){printf("memfd failed errno=%d\n",*__errno());return 2;}
 if(ftruncate(fd,4096))return 3;
 unsigned *v=mmap(0,4096,3,1,fd,0);if(v==(void*)-1)return 4;
 *v=2027;printf("BIONIC MEMFD value=%u\n",*v);munmap(v,4096);close(fd);
 return getpid()==1?0:5;
}
void start_c(void *sp){
 struct {void *preinit;void *init;void *fini;} arrays={0,0,0};
 __libc_init(sp,0,main,&arrays);for(;;){}
}
