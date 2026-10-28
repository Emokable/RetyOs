#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>
#include <sys/wait.h>
#include <sys/stat.h>
#define BASE "/mnt/us/kindle-android"
static int connected_state(const char *path, char *state, size_t capacity) {
    FILE *f=fopen(path,"r");
    if(!f)return 0;
    if(!fgets(state,(int)capacity,f)){fclose(f);return 0;}
    fclose(f);
    state[strcspn(state,"\r\n")]=0;
    // Treat every enumerating or suspended UDC state as an active cable. The
    // earlier guard waited for USB configuration, which can be too late when
    // Kindle's native USB service starts changing gadget functions first.
    static const char *const active[]={
        "attached","powered","default","addressed","configured","suspended","connected"
    };
    for(size_t i=0;i<sizeof(active)/sizeof(active[0]);i++)
        if(!strcmp(state,active[i]))return 1;
    return 0;
}
static int host_connected(const char *test, char *source, size_t source_capacity,
                          char *state, size_t state_capacity) {
    if(source&&source_capacity)source[0]=0;
    if(state&&state_capacity)state[0]=0;
    if(test){
        if(connected_state(test,state,state_capacity)){
            if(source)snprintf(source,source_capacity,"%s",test);
            return 1;
        }
        return 0;
    }
    glob_t paths={0};
    glob("/sys/class/udc/*/state",0,NULL,&paths);
    for(size_t i=0;i<paths.gl_pathc;i++){
        char value[64]={0};
        if(connected_state(paths.gl_pathv[i],value,sizeof(value))){
            if(source)snprintf(source,source_capacity,"%s",paths.gl_pathv[i]);
            if(state)snprintf(state,state_capacity,"%s",value);
            globfree(&paths);return 1;
        }
    }
    globfree(&paths);
    // Some Kindle builds expose the composite gadget state here instead of
    // (or before) the UDC state. This remains read-only and is a fallback.
    const char *legacy="/sys/class/android_usb/android0/state";
    if(connected_state(legacy,state,state_capacity)){
        if(source)snprintf(source,source_capacity,"%s",legacy);
        return 1;
    }
    return 0;
}
static int mtp_frozen_allowed(void){
    char state[64]={0};FILE *f=fopen(BASE "/warm-state","r");
    if(!f)return 0;
    int ok=fgets(state,sizeof(state),f)!=NULL;fclose(f);
    state[strcspn(state,"\r\n")]=0;
    if(!ok||(strcmp(state,"native")&&strcmp(state,"native_pending")))return 0;
    // The session-wide MTP hold proves Android owns the lifecycle. Its frozen
    // exporter runs in a private mount namespace with kindle-android hidden,
    // so USB is safe through the native/frozen handoff.
    if(access(BASE "/mtp-held",R_OK)!=0)return 0;
    f=fopen("/sys/fs/cgroup/kindle-android-warm/session/freezer.state","r");
    if(!f)return 0;
    ok=fgets(state,sizeof(state),f)!=NULL;fclose(f);
    state[strcspn(state,"\r\n")]=0;
    return ok&&!strcmp(state,"FROZEN");
}
static int owns_session(long launcher){
    long owner=0;FILE *f=fopen(BASE "/session.lock/pid","r");
    if(!f)return 0;int ok=fscanf(f,"%ld",&owner)==1;fclose(f);return ok&&owner==launcher;
}
static int valid_launcher(long launcher){
    char path[96],command[4096];snprintf(path,sizeof(path),"/proc/%ld/cmdline",launcher);
    FILE *f=fopen(path,"rb");if(!f)return 0;size_t n=fread(command,1,sizeof(command)-1,f);fclose(f);command[n]=0;
    for(size_t i=0;i<n;){if(!strcmp(command+i,BASE "/start.sh"))return 1;i+=strlen(command+i)+1;}
    return 0;
}
static int read_stage(char *stage,size_t capacity){
    FILE *f=fopen(BASE "/boot-progress.stage","r");if(!f)return 0;
    if(!fgets(stage,capacity,f)){fclose(f);return 0;}fclose(f);
    stage[strcspn(stage,"\r\n")]=0;
    const char *prefix="/mnt/us/kindle-a7-art.";
    if(strncmp(stage,prefix,strlen(prefix))||strlen(stage)!=strlen(prefix)+6)return 0;
    for(const char *p=stage+strlen(prefix);*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')))return 0;
    return 1;
}
static int request_exit(const char *stage){
    DIR *proc=opendir("/proc");if(!proc)return 0;struct dirent *entry;int found=0;
    while((entry=readdir(proc))){
        if(entry->d_name[0]<'1'||entry->d_name[0]>'9')continue;
        char path[512],name[32]={0},root[256]={0},expected[256];
        snprintf(path,sizeof(path),"/proc/%s/comm",entry->d_name);
        FILE *f=fopen(path,"r");if(!f)continue;fgets(name,sizeof(name),f);fclose(f);
        if(strcmp(name,"art-guard\n"))continue;
        snprintf(path,sizeof(path),"/proc/%s/root",entry->d_name);
        ssize_t n=readlink(path,root,sizeof(root)-1);if(n<0)continue;root[n]=0;
        snprintf(expected,sizeof(expected),"%s/rootfs",stage);if(strcmp(root,expected))continue;
        found=1;snprintf(path,sizeof(path),"/proc/%s/root/data/local/tmp/kindle-android-exit",entry->d_name);
        int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
        if(fd>=0){(void)write(fd,"usb-host\n",9);close(fd);}
    }
    closedir(proc);return found;
}
static void stop_mtp(void){
    pid_t child=fork();if(child<0)return;
    if(!child){int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}execl("/sbin/stop","stop","mtp",(char*)NULL);_exit(127);}
    int status=0;
    for(int i=0;i<25;i++){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child){printf("USB_HOST_MTP_STOP_RC=%d\n",WIFEXITED(status)?WEXITSTATUS(status):-1);return;}
        if(result<0&&errno!=EINTR)return;
        usleep(200000);
    }
    kill(child,SIGTERM);while(waitpid(child,&status,0)<0&&errno==EINTR){}
    puts("USB_HOST_MTP_STOP_TIMEOUT=1");
}
static int controller_exit_request(void){
    pid_t child=fork();if(child<0)return 0;
    if(!child){int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}execl(BASE "/session-controller", "session-controller", "request", "exit", (char*)NULL);_exit(127);}
    int status=0,reaped=0;
    for(int i=0;i<20;i++){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child){reaped=1;if(WIFEXITED(status)&&WEXITSTATUS(status)==0){puts("USB_HOST_CONTROLLER_EXIT_REQUESTED=1");return 1;}break;}
        if(result<0&&errno!=EINTR){if(errno==ECHILD)reaped=1;break;}
        usleep(100000);
    }
    if(!reaped){kill(child,SIGTERM);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
    // The socket controller may briefly be restarting. Its watched command
    // file is the durable fallback and also thaws a retained Android session.
    char temp[256];snprintf(temp,sizeof(temp),BASE "/warm.command.new");
    int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    int ok=write(fd,"exit\n",5)==5&&fsync(fd)==0;close(fd);
    if(!ok||rename(temp,BASE "/warm.command")!=0){unlink(temp);return 0;}
    puts("USB_HOST_CONTROLLER_EXIT_QUEUED=1");return 1;
}
static int script(const char *path,const char *arg){
    pid_t child=fork();if(child<0)return -1;
    if(!child){int fd=open("/dev/null",O_RDONLY);if(fd>=0){dup2(fd,0);close(fd);}
        if(arg)execl("/bin/sh","sh",path,arg,"--cleanup-only",(char*)NULL);
        else execl("/bin/sh","sh",path,(char*)NULL);_exit(127);}
    int status;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return -1;}
    return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
int main(int argc,char **argv){
    setbuf(stdout,NULL);
    if(argc==2&&!strcmp(argv[1],"--check"))return host_connected(NULL,NULL,0,NULL,0)?2:0;
    if(argc<2||argc>3)return 2;char *end;long launcher=strtol(argv[1],&end,10);
    if(*end||launcher<=1)return 2;
    const char *test=argc==3?argv[2]:NULL;
    if(test&&strcmp(test,BASE "/usb-test.state"))return 2;
    if(!valid_launcher(launcher)||!owns_session(launcher)){puts("USB_GUARD_REFUSED_LAUNCHER_IDENTITY=1");return 2;}
    int requested=0,ticks=0,had_guard=0,mtp_allowed_logged=0;char stage[256]={0};
    while(kill((pid_t)launcher,0)==0){
        if(!owns_session(launcher))return 0;
        char source[256]={0},state[64]={0};
        if(!requested&&host_connected(NULL,source,sizeof(source),state,sizeof(state))){
            if(mtp_frozen_allowed()){
                if(!mtp_allowed_logged){printf("USB_HOST_MTP_ALLOWED_FROZEN=1 source=%s state=%s\n",source,state);mtp_allowed_logged=1;}
            }else{
                requested=1;printf("USB_HOST_EXIT_REQUESTED=1 source=%s state=%s\n",source,state);stop_mtp();
                if(controller_exit_request())had_guard=1;
            }
        }
        if(!requested&&test&&host_connected(test,source,sizeof(source),state,sizeof(state))){
            if(mtp_frozen_allowed()){
                if(!mtp_allowed_logged){printf("USB_HOST_MTP_ALLOWED_FROZEN=1 source=%s state=%s\n",source,state);mtp_allowed_logged=1;}
            }else{
                requested=1;printf("USB_HOST_EXIT_REQUESTED=1 source=%s state=%s\n",source,state);stop_mtp();
                if(controller_exit_request())had_guard=1;
            }
        }
        if(requested){
            if(!had_guard&&read_stage(stage,sizeof(stage))&&request_exit(stage)){had_guard=1;}
            else if(!had_guard&&++ticks>=10){kill((pid_t)launcher,SIGTERM);puts("USB_HOST_BOOT_ABORT_REQUESTED=1");break;}
        }
        usleep(200000);
    }
    for(int i=0;i<150&&kill((pid_t)launcher,0)==0;i++)usleep(200000);
    if(kill((pid_t)launcher,0)==0){puts("USB_GUARD_RECOVERY_DEFERRED_LAUNCHER_ACTIVE=1");return 3;}
    // If the launcher was killed externally, recover only its validated stage.
    if(owns_session(launcher)&&access(BASE "/framework-stopped",F_OK)==0){
        if(read_stage(stage,sizeof(stage))){
            for(int i=0;i<150&&request_exit(stage);i++)usleep(200000);
            if(request_exit(stage)){puts("USB_GUARD_RECOVERY_DEFERRED_ANDROID_ACTIVE=1");return 3;}
            char run[320];snprintf(run,sizeof(run),"%s/run.sh",stage);
            if(access(run,R_OK)==0)printf("USB_GUARD_STAGE_CLEANUP=%d\n",script(run,stage));
        }
        printf("USB_GUARD_FRAMEWORK_RESTORE=%d\n",script(BASE "/restore-framework.sh",NULL));
    }
    return 0;
}
