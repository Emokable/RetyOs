#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#define BASE "/mnt/us/kindle-android"
#define ROOT "/sys/fs/cgroup/kindle-android-warm"
#define GROUP ROOT "/session"
#define OWNER BASE "/warm-freezer.stage"
static int put(const char *p,const char *s){int f=open(p,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return -1;size_t n=strlen(s);int ok=write(f,s,n)==(ssize_t)n;close(f);return ok?0:-1;}
static int get(const char *p,char *s,size_t n){FILE *f=fopen(p,"r");if(!f)return -1;int ok=fgets(s,(int)n,f)!=NULL;fclose(f);if(ok)s[strcspn(s,"\r\n")]=0;return ok?0:-1;}
static int valid_stage(const char *s){const char *prefix="/mnt/us/kindle-a7-art.";size_t n=strlen(prefix);if(strncmp(s,prefix,n)||strlen(s)!=n+6)return 0;for(s+=n;*s;s++)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')))return 0;return 1;}
static int rooted(long pid,const char *stage){char p[80],actual[512],want[256];snprintf(p,sizeof(p),"/proc/%ld/root",pid);ssize_t n=readlink(p,actual,sizeof(actual)-1);if(n<0)return 0;actual[n]=0;snprintf(want,sizeof(want),"%s/rootfs",stage);return !strcmp(actual,want);}
static int members(const char *stage,int empty){FILE *f=fopen(GROUP "/cgroup.procs","r");if(!f)return errno==ENOENT?0:-1;long pid;int count=0;while(fscanf(f,"%ld",&pid)==1){if(pid==getpid()||empty||!rooted(pid,stage)){if(kill((pid_t)pid,0)==0){fclose(f);return -1;}}count++;}fclose(f);return count;}
static int owns(const char *stage){char s[256];return get(OWNER,s,sizeof(s))==0&&!strcmp(s,stage);}
static int prepare(const char *stage){struct stat st;if(lstat(ROOT,&st)==0&&(!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode)))return 2;if(mkdir(ROOT,0700)&&errno!=EEXIST)return 3;struct statfs fs;if(statfs(ROOT,&fs))return 4;if((unsigned long)fs.f_type!=0x27e0ebUL&&mount("none",ROOT,"cgroup",0,"freezer,name=kindle_android_freezer"))return 5;if(mkdir(GROUP,0700)&&errno!=EEXIST)return 6;if(access(GROUP "/freezer.state",W_OK))return 7;if(members(stage,1)!=0)return 8;if(put(GROUP "/freezer.state","THAWED"))return 9;int f=open(OWNER,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 10;int n=dprintf(f,"%s\n",stage);close(f);return n>0?0:11;}
static int freeze(const char *stage){if(!owns(stage)||members(stage,0)<0)return 12;int count=0,guard=0,system=0;
 for(int pass=0;pass<3;pass++){DIR *d=opendir("/proc");if(!d)return 13;struct dirent *e;count=guard=system=0;while((e=readdir(d))){char *end;long pid=strtol(e->d_name,&end,10);if(*end||pid<=1||!rooted(pid,stage))continue;char path[80],name[80]={0},value[32];snprintf(path,sizeof(path),"/proc/%ld/comm",pid);get(path,name,sizeof(name));if(!strcmp(name,"art-guard"))guard++;if(!strcmp(name,"system_server"))system++;snprintf(value,sizeof(value),"%ld",pid);if(put(GROUP "/cgroup.procs",value)&&errno!=ESRCH&&errno!=ENOENT){closedir(d);return 14;}count++;}closedir(d);}
 if(count<8||guard!=1||system!=1||members(stage,0)<0)return 15;
 if(put(GROUP "/freezer.state","FROZEN"))return 16;
 for(int n=0;n<80;n++){char s[32];if(!get(GROUP "/freezer.state",s,sizeof(s))&&!strcmp(s,"FROZEN")){printf("WARM_FREEZER_FROZEN processes=%d\n",count);return 0;}usleep(50000);}
 put(GROUP "/freezer.state","THAWED");return 17;
}
int main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"now")){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);printf("%lld\n",(long long)t.tv_sec*1000000000+t.tv_nsec);return 0;}if(argc!=3||!valid_stage(argv[2]))return 2;int rc=0;
 if(!strcmp(argv[1],"guard")){DIR *d=opendir("/proc");if(!d)return 1;struct dirent *e;long found=0;while((e=readdir(d))){char *end;long pid=strtol(e->d_name,&end,10);if(*end||pid<=1||!rooted(pid,argv[2]))continue;char p[80],s[80];snprintf(p,sizeof(p),"/proc/%ld/comm",pid);if(!get(p,s,sizeof(s))&&!strcmp(s,"art-guard")){found=pid;break;}}closedir(d);if(!found)return 1;printf("%ld\n",found);return 0;}
 else if(!strcmp(argv[1],"prepare"))rc=prepare(argv[2]);
 else if(!strcmp(argv[1],"freeze"))rc=freeze(argv[2]);
 else if(!strcmp(argv[1],"thaw")){if(access(OWNER,F_OK)==0&&!owns(argv[2]))return 18;if(access(GROUP "/freezer.state",F_OK)==0)rc=put(GROUP "/freezer.state","THAWED")?19:0;}
 else if(!strcmp(argv[1],"release")){if(access(OWNER,F_OK)==0&&!owns(argv[2]))return 18;if(access(GROUP,F_OK)==0){if(put(GROUP "/freezer.state","THAWED")||members(argv[2],1)!=0)return 20;if(rmdir(GROUP))return 21;}if(access(ROOT,F_OK)==0){if(umount(ROOT)&&errno!=EINVAL)return 22;if(rmdir(ROOT))return 23;}unlink(OWNER);}
 else if(!strcmp(argv[1],"status")){char s[32];if(get(GROUP "/freezer.state",s,sizeof(s)))return 24;puts(s);}
 else return 2;
 if(rc)fprintf(stderr,"WARM_FREEZER_ERROR action=%s rc=%d errno=%d\n",argv[1],rc,errno);return rc;
}
