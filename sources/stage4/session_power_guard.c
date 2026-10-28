#define _GNU_SOURCE
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#define BASE "/mnt/us/kindle-android"
#define STATE BASE "/power-guard.restore"
static volatile sig_atomic_t stop=0;
static int locked=0,brightness=0,original_prevent=0,owns_state=0;
static char marker[256],gesture[256],exit_path[256];
static void signal_stop(int sig){(void)sig;stop=1;}
static void warm_state(const char *s){FILE *f=fopen(BASE "/warm-power.state.new","w");if(f){fprintf(f,"%s\n",s);fclose(f);rename(BASE "/warm-power.state.new",BASE "/warm-power.state");}}
static long long millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int property(const char *name,int value){char val[20];snprintf(val,sizeof(val),"%d",value);pid_t p=fork();if(p==0){execl("/usr/bin/lipc-set-prop","lipc-set-prop","-i","--","com.lab126.powerd",name,val,(char*)0);_exit(127);}if(p<0)return -1;int s;while(waitpid(p,&s,0)<0){if(errno!=EINTR)return -1;}return WIFEXITED(s)?WEXITSTATUS(s):-1;}
static int read_property(const char *name){char cmd[160];snprintf(cmd,sizeof(cmd),"/usr/bin/lipc-get-prop -i com.lab126.powerd %s 2>/dev/null",name);FILE *f=popen(cmd,"r");if(!f)return -1;int v=-1;if(fscanf(f,"%d",&v)!=1)v=-1;pclose(f);return v;}
static int save_state(void){int fd=open(STATE ".new",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;FILE *f=fdopen(fd,"w");if(!f){close(fd);return -1;}fprintf(f,"%d %d %d\n",original_prevent,brightness,locked);int rc=fflush(f);if(!rc)rc=fsync(fd);if(fclose(f))rc=-1;if(!rc)rc=rename(STATE ".new",STATE);if(!rc)owns_state=1;return rc;}
static int restore(void){FILE *f=fopen(STATE,"r");if(!f)return 0;int prevent=-1,light=-1,was_locked=1;int valid=fscanf(f,"%d %d %d",&prevent,&light,&was_locked)>=2;fclose(f);if(!valid||prevent<0||prevent>1||light<0||light>24)return 2;
 int a=was_locked?property("flIntensity",light):0,b=property("preventScreenSaver",prevent);if(!a&&!b){unlink(STATE);printf("POWER_GUARD_RESTORED light=%d prevent=%d\n",light,prevent);return 0;}return 3;}
static void set_locked(int value){if(value==locked)return;
 if(value){int current=read_property("flIntensity");if(current>=0&&current<=24)brightness=current;int fd=open(marker,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);if(fd<0)return;write(fd,"1\n",2);close(fd);locked=1;save_state();property("flIntensity",0);}
 else{unlink(marker);
  /* The compositor may have consumed its last frame while the panel was
   * locked. Ask the existing worker to repaint that snapshot on wake. */
  char resume[256];snprintf(resume,sizeof(resume),"%s",marker);
  char *name=strrchr(resume,'/');if(name)strcpy(name+1,"kindle-display-resume");
  int repaint=open(resume,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
  if(repaint>=0){write(repaint,"1\n",2);close(repaint);}else perror("POWER_GUARD_WAKE_REPAINT");
  property("flIntensity",brightness);locked=0;save_state();}
 printf("POWER_GUARD_LOCKED=%d monotonic_ms=%lld\n",locked,millis());fflush(stdout);
}
static long find_target(const char *stage){DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;long found=0;char expected[220];snprintf(expected,sizeof(expected),"%s/rootfs",stage);
 while((e=readdir(d))){char *end;long pid=strtol(e->d_name,&end,10);if(*end||pid<=1)continue;char path[256],name[32]={0},root[256]={0};snprintf(path,sizeof(path),"/proc/%ld/comm",pid);FILE *f=fopen(path,"r");if(!f)continue;fgets(name,sizeof(name),f);fclose(f);if(strcmp(name,"art-guard\n"))continue;snprintf(path,sizeof(path),"/proc/%ld/root",pid);ssize_t n=readlink(path,root,sizeof(root)-1);if(n>0){root[n]=0;if(!strcmp(root,expected)){found=pid;break;}}}closedir(d);return found;}
int main(int argc,char **argv){setvbuf(stdout,0,_IOLBF,0);signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);signal(SIGHUP,signal_stop);
 if(argc==2&&!strcmp(argv[1],"--restore"))return restore();
 if(argc!=2)return 2;char *end;long target=0;
 if(!strncmp(argv[1],"/mnt/us/kindle-a7-art.",21)){
  for(int n=0;n<1800&&!stop;n++){target=find_target(argv[1]);if(target)break;usleep(100000);}if(!target)return 8;
 }else{target=strtol(argv[1],&end,10);if(*end||target<=1)return 2;}
 snprintf(marker,sizeof(marker),"/proc/%ld/root/data/local/tmp/kindle-session-locked",target);
 snprintf(gesture,sizeof(gesture),"/proc/%ld/root/data/local/tmp/kindle-page-direction",target);
 snprintf(exit_path,sizeof(exit_path),"/proc/%ld/root/data/local/tmp/kindle-android-exit",target);
 if(restore()!=0)return 3;
 int fd=open("/dev/input/event0",O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 4;
 char name[80]={0};if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||!strstr(name,"pwrkey")||ioctl(fd,EVIOCGRAB,1)<0){close(fd);return 5;}
 original_prevent=read_property("preventScreenSaver");brightness=read_property("flIntensity");
 if(original_prevent<0||original_prevent>1||brightness<0||brightness>24||save_state()!=0){close(fd);return 6;}
 if(property("preventScreenSaver",1)!=0){restore();close(fd);return 7;}
 property("wakeUp",1);
 unlink(marker);signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);signal(SIGHUP,signal_stop);
 printf("POWER_GUARD_READY device=%s idle_seconds=600 mode=screen_lock no_deep_suspend=1\n",name);
 warm_state("android");int native_handoff=0;
 long long pressed=0,last_activity=millis();struct timespec last_stamp={0,0};
 while(!stop&&kill((pid_t)target,0)==0){
  if(access(BASE "/warm-native",F_OK)==0){
   if(!native_handoff){if(ioctl(fd,EVIOCGRAB,0)||restore()!=0){stop=1;break;}owns_state=0;native_handoff=1;warm_state("native");puts("POWER_GUARD_NATIVE_HANDOFF=1");}
   usleep(100000);continue;
  }
  if(native_handoff){
   if(ioctl(fd,EVIOCGRAB,1)){stop=1;break;}
   struct input_event stale[16];while(read(fd,stale,sizeof(stale))>0){}
   property("wakeUp",1);
   original_prevent=read_property("preventScreenSaver");brightness=read_property("flIntensity");locked=0;
   if(original_prevent<0||original_prevent>1||brightness<0||brightness>24||save_state()!=0||property("preventScreenSaver",1)){stop=1;break;}
   native_handoff=0;pressed=0;last_activity=millis();warm_state("android");puts("POWER_GUARD_ANDROID_REACQUIRED=1");
  }
  struct pollfd p={fd,POLLIN,0};poll(&p,1,250);struct input_event ev[16];ssize_t bytes=read(fd,ev,sizeof(ev));
  if(bytes>0)for(ssize_t i=0;i<bytes/(ssize_t)sizeof(ev[0]);i++)if(ev[i].type==EV_KEY&&ev[i].code==KEY_POWER){
   if(ev[i].value==1)pressed=millis();else if(ev[i].value==0&&pressed){long long held=millis()-pressed;pressed=0;
    if(held>=3000){int x=open(exit_path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644);if(x>=0){write(x,"power-long-press\n",17);close(x);}printf("POWER_GUARD_EXIT_REQUEST=long_press\n");break;}
    puts("POWER_GUARD_BUTTON=short_press");set_locked(!locked);last_activity=millis();}}
  struct stat st;if(stat(gesture,&st)==0&&(st.st_mtim.tv_sec!=last_stamp.tv_sec||st.st_mtim.tv_nsec!=last_stamp.tv_nsec)){last_stamp=st.st_mtim;last_activity=millis();}
  if(!locked&&millis()-last_activity>=600000){puts("POWER_GUARD_IDLE_LOCK=1");set_locked(1);}
  FILE *command=fopen(BASE "/power-guard.command","r");if(command){char s[16]={0};fgets(s,sizeof(s),command);fclose(command);unlink(BASE "/power-guard.command");if(!strcmp(s,"lock\n"))set_locked(1);else if(!strcmp(s,"wake\n")){set_locked(0);last_activity=millis();}else if(!strcmp(s,"idle-test\n"))last_activity=millis()-600001;}
 }
 unlink(marker);if(owns_state)restore();ioctl(fd,EVIOCGRAB,0);close(fd);warm_state("stopped");puts("POWER_GUARD_STOPPED=1");return 0;
}
