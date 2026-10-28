/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sched.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdlib.h>
#include <grp.h>
#include <time.h>
#include <poll.h>
#include <arpa/inet.h>
static int logger=-1, zsocket=-1;
static int propfd=-1;
static int installdfd=-1;
static int lmkdfd=-1,lmkdclient=-1;
static int netd_probe=0;
static const char *netd_socket_names[]={"netd","dnsproxyd","mdns","fwmarkd"};
static int netd_sockets[4]={-1,-1,-1,-1};
static int vold_probe=0,vold_sockets[2]={-1,-1};
static int audio_probe=0;
static int system_uid_probe=0;
static int keystore_probe=0;
static int zygote_hello_result=0,zygote_hello_uid=0,zygote_hello_gid=0;
static pid_t system_server_pid=0;
static int system_server_phase550_seen=0,system_server_trace_dumped=0;
#define USER0_BOOT_READY_FILE "/data/local/tmp/kindlelab-user0-boot-ready"
static pid_t textservices_stub=-1;
static int unix_socket(const char *path,int type);
static int check_service(const char *name){
    pid_t query=fork();if(query<0)return -1;
    if(!query){
        struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
        struct __user_cap_data_struct c[2]={{0}};
        if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0))_exit(125);
        execl("/system/bin/sf-service-check","sf-service-check",name,(char*)NULL);_exit(127);
    }
    int status;
    if(waitpid(query,&status,0)!=query || !WIFEXITED(status))return -1;
    return WEXITSTATUS(status);
}
static int properties(int android_session){
    mkdir("/dev/__properties__",0755);mkdir("/dev/socket",0755);
    int rc=__system_property_area_init();
    if(rc!=0 && rc!=-2)return -1;
    const char *paths[]={"/default.prop","/system/build.prop"};
    for(unsigned path=0;path<2;path++){
    FILE *props=fopen(paths[path],"r");char line[512];
    if(!props){if(path==1)return -1;continue;}
    while(fgets(line,sizeof(line),props)){
        if(line[0]=='#')continue;
        char *eq=strchr(line,'=');if(!eq)continue;*eq++=0;
        eq[strcspn(eq,"\r\n")]=0;
        if(strlen(line)<PROP_NAME_MAX && strlen(eq)<PROP_VALUE_MAX && !__system_property_find(line))
            if(__system_property_add(line,strlen(line),eq,strlen(eq))){fclose(props);return -1;}
    }
    fclose(props);
    }
    const char *pairs[][2]={
        {"ro.product.cpu.abilist32","armeabi-v7a,armeabi"},
        {"ro.product.cpu.abilist","armeabi-v7a,armeabi"},
        {"ro.product.cpu.abi","armeabi-v7a"},
        {"ro.boot.container","1"},{"ro.zygote.disable_gl_preload","1"},
        {"ro.gfx.driver.0","headless-lab-no-driver"},
        {"ro.sf.lcd_density","360"},{"qemu.hw.mainkeys","0"},
        {"persist.sys.timezone","Asia/Shanghai"},
        {"dalvik.vm.heapsize",android_session?"128m":"32m"},{"dalvik.vm.heapstartsize",android_session?"16m":"8m"},
        {"dalvik.vm.usejit",android_session?"true":"false"},{"dalvik.vm.dex2oat-flags",android_session?"":"--compiler-filter=verify-none"},
        /* Reader sessions trade a longer one-time install for native app code.
         * Keep disposable compatibility probes cheap and avoid profile loss
         * for code loaded dynamically after installation. */
        {"pm.dexopt.install",android_session?"speed":"interpret-only"}
        ,{"dalvik.vm.usejitprofiles",android_session?"true":"false"}
    };
    for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);i++){
        const prop_info *existing=__system_property_find(pairs[i][0]);
        int status=existing?__system_property_update((prop_info*)existing,pairs[i][1],strlen(pairs[i][1])):
            __system_property_add(pairs[i][0],strlen(pairs[i][0]),pairs[i][1],strlen(pairs[i][1]));
        if(status)return -1;
    }
    propfd=unix_socket("/dev/socket/property_service",SOCK_STREAM);
    if(propfd<0 || listen(propfd,8))return -1;
    chmod("/dev/socket/property_service",0666);
    fprintf(stderr,"ZY_PROPERTY_AREA=%d\n",rc);return 0;
}
static int unix_socket(const char *path,int type){
    int fd=socket(AF_UNIX,type,0);struct sockaddr_un a={.sun_family=AF_UNIX};
    if(fd<0)return -1;strcpy(a.sun_path,path);
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))){close(fd);return -1;}
    return fd;
}
static pid_t start_fuse_mount_broker(void){
    pid_t broker=fork();if(broker<0)return -1;
    if(!broker){
        if(logger>=0)close(logger);if(propfd>=0)close(propfd);if(installdfd>=0)close(installdfd);
        struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
        struct __user_cap_data_struct c[2]={{0}};
        unsigned word=CAP_SYS_ADMIN/32,bit=1U<<(CAP_SYS_ADMIN%32);
        c[word].effective=c[word].permitted=c[word].inheritable=bit;
        if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0) ||
           prctl(47,2,CAP_SYS_ADMIN,0,0)){
            fprintf(stderr,"FUSE_BROKER_CAPABILITY_SETUP_FAILED errno=%d\n",errno);_exit(125);
        }
        execl("/system/bin/kindlelab-fuse-mount-broker",
              "kindlelab-fuse-mount-broker",(char*)NULL);
        perror("FUSE_BROKER_EXEC");_exit(127);
    }
    for(int i=0;i<100;i++){
        int probe=socket(AF_UNIX,SOCK_SEQPACKET,0);
        if(probe>=0){
            struct sockaddr_un address={.sun_family=AF_UNIX};
            strcpy(address.sun_path,"/dev/socket/kindlelab-fuse-mount");
            if(!connect(probe,(struct sockaddr*)&address,sizeof(address))){
                close(probe);
                fprintf(stderr,"FUSE_BROKER_PARENT_READY pid=%d\n",broker);
                return broker;
            }
            close(probe);
        }
        int status=0;
        if(waitpid(broker,&status,WNOHANG)==broker){
            fprintf(stderr,"FUSE_BROKER_EARLY_EXIT status=%d\n",status);return -1;
        }
        usleep(50000);
    }
    kill(broker,SIGKILL);waitpid(broker,NULL,0);
    fprintf(stderr,"FUSE_BROKER_READY_TIMEOUT=1\n");return -1;
}
static void drain_lmkd(void){
    if(lmkdfd<0)return;
    if(lmkdclient<0){
        int fd=accept(lmkdfd,NULL,NULL);
        if(fd>=0){fcntl(fd,F_SETFL,O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);lmkdclient=fd;
            fprintf(stderr,"LMKD_COMPAT_CONNECTED=1 memory_policy=kernel-oom\n");}
    }
    if(lmkdclient>=0){
        char packet[256];ssize_t n;
        while((n=recv(lmkdclient,packet,sizeof(packet),MSG_DONTWAIT))>0){}
        if(n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)){
            close(lmkdclient);lmkdclient=-1;
        }
    }
}
static void drain_logs(void){
    drain_lmkd();
    char b[8192];int n;
    if(logger<0)return;
    while((n=recv(logger,b,sizeof(b)-1,MSG_DONTWAIT))>0){
        /* logdw datagrams contain a binary header then priority/tag/message. */
        if(n<12 || b[0]==2)continue; /* binary events are not text */
        if ((unsigned char)b[11] > 0 && (unsigned char)b[11] < 4) continue;
        for(int i=12;i<n;i++)if((unsigned char)b[i]<32 && b[i]!='\n')b[i]=' ';
        b[n]=0;
        if(strstr(b+12,"HELLO_ART_RESULT=2027"))zygote_hello_result=1;
        if(strstr(b+12,"HELLO_ART_UID=1000 1000 1000 1000"))zygote_hello_uid=1;
        if(strstr(b+12,"HELLO_ART_GID=1000 1000 1000 1000"))zygote_hello_gid=1;
        if(strstr(b+12,"Starting phase 550"))system_server_phase550_seen=1;
        if(strstr(b+12,"Finishing user boot 0")){
            int ready=open(USER0_BOOT_READY_FILE,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
            if(ready>=0){(void)write(ready,"USER0_RUNNING_LOCKED_PENDING\n",29);(void)fsync(ready);close(ready);}
            fprintf(stderr,"ANDROID_USER0_BOOT_PHASE_OBSERVED=1 source=ActivityManager\n");
        }
        fprintf(stderr,"ANDROID_LOG %s\n",b+12);
    }
}
static int write_exact(int fd,const void *buffer,size_t length){
    size_t have=0;while(have<length){ssize_t n=write(fd,(const char*)buffer+have,length-have);if(n<=0)return -1;have+=(size_t)n;}return 0;
}
static int report_zygote_child_identity(pid_t pid){
    char path[64],line[256];unsigned uid=0,gid=0;int have_uid=0,have_gid=0;
    snprintf(path,sizeof(path),"/proc/%d/status",pid);FILE *f=fopen(path,"r");if(!f)return 0;
    while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"Uid: %u",&uid)==1)have_uid=1;
        if(sscanf(line,"Gid: %u",&gid)==1)have_gid=1;
    }
    fclose(f);
    if(have_uid&&have_gid){
        fprintf(stderr,"ZYGOTE_CHILD_STATUS pid=%d uid=%u gid=%u\n",pid,uid,gid);
        return uid==1000&&gid==1000?1:-1;
    }
    return 0;
}
static void report_zygote_open_fds(pid_t pid){
    char dir_path[64],fd_path[96],target[512];
    snprintf(dir_path,sizeof(dir_path),"/proc/%d/fd",pid);
    DIR *fds=opendir(dir_path);
    if(!fds){fprintf(stderr,"ZYGOTE_FDS_UNAVAILABLE pid=%d errno=%d\n",pid,errno);return;}
    struct dirent *entry;unsigned count=0;
    while(count<64 && (entry=readdir(fds))!=NULL){
        const char *n=entry->d_name;int numeric=*n!=0;
        for(const char *c=n;*c;c++)if(*c<'0'||*c>'9')numeric=0;
        if(!numeric)continue;
        snprintf(fd_path,sizeof(fd_path),"%s/%s",dir_path,n);
        ssize_t length=readlink(fd_path,target,sizeof(target)-1);
        if(length<0)snprintf(target,sizeof(target),"<readlink errno=%d>",errno);
        else target[length]=0;
        fprintf(stderr,"ZYGOTE_FD pid=%d fd=%s target=%s\n",pid,n,target);
        count++;
    }
    closedir(fds);
    if(count==64)fprintf(stderr,"ZYGOTE_FDS_TRUNCATED pid=%d limit=64\n",pid);
}
static void report_zygote_server_state(pid_t pid){
    char path[96],line[256];
    const char *status_keys[]={"Name:","State:","Pid:","PPid:","Uid:","Gid:",
                               "VmRSS:","VmSize:","Threads:","Seccomp:","NoNewPrivs:"};
    snprintf(path,sizeof(path),"/proc/%d/status",pid);
    FILE *f=fopen(path,"r");
    fprintf(stderr,"ZYGOTE_SERVER_STATE pid=%d\n",pid);
    if(f){
        while(fgets(line,sizeof(line),f))
            for(unsigned i=0;i<sizeof(status_keys)/sizeof(status_keys[0]);i++)
                if(!strncmp(line,status_keys[i],strlen(status_keys[i]))){fputs(line,stderr);break;}
        fclose(f);
    }else{
        fprintf(stderr,"ZYGOTE_SERVER_STATUS_UNAVAILABLE errno=%d\n",errno);
    }
    const char *files[]={"wchan","syscall"};
    for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++){
        snprintf(path,sizeof(path),"/proc/%d/%s",pid,files[i]);
        f=fopen(path,"r");
        if(f){
            if(fgets(line,sizeof(line),f)){
                line[strcspn(line,"\r\n")]=0;
                fprintf(stderr,"ZYGOTE_SERVER_%s=%s\n",files[i],line);
            }else fprintf(stderr,"ZYGOTE_SERVER_%s_EMPTY\n",files[i]);
            fclose(f);
        }else{
            fprintf(stderr,"ZYGOTE_SERVER_%s_UNAVAILABLE errno=%d\n",files[i],errno);
        }
    }
    char task_dir[96];
    snprintf(task_dir,sizeof(task_dir),"/proc/%d/task",pid);
    DIR *tasks=opendir(task_dir);
    if(!tasks){fprintf(stderr,"ZYGOTE_SERVER_TASKS_UNAVAILABLE errno=%d\n",errno);return;}
    struct dirent *entry;unsigned sampled=0;
    while(sampled<16 && (entry=readdir(tasks))!=NULL){
        const char *tid=entry->d_name;int numeric=*tid!=0;
        for(const char *c=tid;*c;c++)if(*c<'0'||*c>'9')numeric=0;
        if(!numeric)continue;
        char base[128],sample_path[192],comm[128]="",state[128]="",
             wchan[128]="",syscall_line[256]="",stat[512]="";
        snprintf(base,sizeof(base),"%s/%s",task_dir,tid);
        snprintf(sample_path,sizeof(sample_path),"%s/comm",base);
        FILE *f=fopen(sample_path,"r");if(f){if(fgets(comm,sizeof(comm),f))comm[strcspn(comm,"\r\n")]=0;fclose(f);}
        snprintf(sample_path,sizeof(sample_path),"%s/status",base);
        f=fopen(sample_path,"r");
        if(f){while(fgets(state,sizeof(state),f))if(!strncmp(state,"State:",6)){state[strcspn(state,"\r\n")]=0;break;}fclose(f);}
        snprintf(sample_path,sizeof(sample_path),"%s/wchan",base);
        f=fopen(sample_path,"r");if(f){if(fgets(wchan,sizeof(wchan),f))wchan[strcspn(wchan,"\r\n")]=0;fclose(f);}
        snprintf(sample_path,sizeof(sample_path),"%s/syscall",base);
        f=fopen(sample_path,"r");if(f){if(fgets(syscall_line,sizeof(syscall_line),f))syscall_line[strcspn(syscall_line,"\r\n")]=0;fclose(f);}
        snprintf(sample_path,sizeof(sample_path),"%s/stat",base);
        f=fopen(sample_path,"r");if(f){if(fgets(stat,sizeof(stat),f))stat[strcspn(stat,"\r\n")]=0;fclose(f);}
        fprintf(stderr,"ZYGOTE_TID tid=%s comm=%s %s wchan=%s syscall=%s\n",
                tid,comm,state,wchan,syscall_line);
        if(stat[0])fprintf(stderr,"ZYGOTE_TID_STAT tid=%s %s\n",tid,stat);
        sampled++;
    }
    closedir(tasks);
    if(sampled==16)fprintf(stderr,"ZYGOTE_SERVER_TASKS_TRUNCATED limit=16\n");
}
static void report_zygote_java_traces(void){
    struct timespec delay={.tv_sec=1,.tv_nsec=0};
    while(nanosleep(&delay,&delay)<0 && errno==EINTR){}
    int fd=open("/data/anr/traces.txt",O_RDONLY|O_CLOEXEC);
    if(fd<0){fprintf(stderr,"ZYGOTE_TRACE_OPEN_FAILED errno=%d\n",errno);return;}
    fprintf(stderr,"ZYGOTE_TRACE_BEGIN\n");
    char buffer[2048];size_t total=0;
    while(total<65536){
        size_t want=sizeof(buffer);if(want>65536-total)want=65536-total;
        ssize_t n=read(fd,buffer,want);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        size_t written=0;
        while(written<(size_t)n){
            ssize_t out=write(STDERR_FILENO,buffer+written,(size_t)n-written);
            if(out<0 && errno==EINTR)continue;
            if(out<=0)break;
            written+=(size_t)out;
        }
        total+=(size_t)n;
        if(written<(size_t)n)break;
    }
    close(fd);fprintf(stderr,"\nZYGOTE_TRACE_END bytes=%zu\n",total);
}
static void report_system_server_java_traces(pid_t pid){
    char path[96];
    snprintf(path,sizeof(path),"/proc/%d/root/data/anr/traces.txt",pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0){fprintf(stderr,"APK_SYSTEM_SERVER_TRACE_OPEN_FAILED pid=%d errno=%d\n",pid,errno);return;}
    fprintf(stderr,"APK_SYSTEM_SERVER_TRACE_BEGIN pid=%d\n",pid);
    char buffer[2048];size_t total=0;
    while(total<65536){
        size_t want=sizeof(buffer);if(want>65536-total)want=65536-total;
        ssize_t n=read(fd,buffer,want);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        ssize_t written=0;
        while(written<n){
            ssize_t out=write(STDERR_FILENO,buffer+written,(size_t)(n-written));
            if(out<0 && errno==EINTR)continue;
            if(out<=0)break;
            written+=out;
        }
        total+=(size_t)n;
        if(written<n)break;
    }
    close(fd);fprintf(stderr,"\nAPK_SYSTEM_SERVER_TRACE_END bytes=%zu\n",total);
}
static void report_getsockname_trace(void){
    int fd=open("/data/local/tmp/getsockname-trace.log",O_RDONLY|O_CLOEXEC);
    if(fd<0){fprintf(stderr,"GETSOCKNAME_TRACE_UNAVAILABLE errno=%d\n",errno);return;}
    char buffer[1024];size_t total=0;ssize_t n;
    fprintf(stderr,"GETSOCKNAME_TRACE_BEGIN\n");
    while(total<16384 && (n=read(fd,buffer,sizeof(buffer)))>0){
        fwrite(buffer,1,(size_t)n,stderr);total+=(size_t)n;
    }
    close(fd);fprintf(stderr,"GETSOCKNAME_TRACE_END bytes=%zu\n",total);
}
static void report_system_server_caps(void){
    static int reported=0;
    if(reported)return;
    DIR *proc=opendir("/proc");if(!proc)return;
    struct dirent *entry;
    while((entry=readdir(proc))!=NULL){
        if(entry->d_name[0]<'0'||entry->d_name[0]>'9')continue;
        char path[128],line[512],name[64]={0};unsigned uid=0;
        unsigned long long eff=0,prm=0,bnd=0;
        snprintf(path,sizeof(path),"/proc/%s/status",entry->d_name);
        FILE *f=fopen(path,"r");if(!f)continue;
        while(fgets(line,sizeof(line),f)){
            if(sscanf(line,"Name: %63s",name)==1)continue;
            if(sscanf(line,"Uid: %u",&uid)==1)continue;
            if(sscanf(line,"CapEff: %llx",&eff)==1)continue;
            if(sscanf(line,"CapPrm: %llx",&prm)==1)continue;
            if(sscanf(line,"CapBnd: %llx",&bnd)==1)continue;
        }
        fclose(f);
        if(!strcmp(name,"system_server")||(!strcmp(name,"app_process32")&&uid==1000)){
            fprintf(stderr,"SYSTEM_SERVER_CAPS pid=%s uid=%u name=%s eff=%llx prm=%llx bnd=%llx sys_nice=%d\n",
                    entry->d_name,uid,name,eff,prm,bnd,(int)((eff>>CAP_SYS_NICE)&1));
            system_server_pid=(pid_t)atoi(entry->d_name);reported=1;break;
        }
    }
    closedir(proc);
}
static void probe_uid1000_setpriority(void){
    pid_t test=fork();if(test<0){fprintf(stderr,"UID1000_SETPRIORITY_PROBE_FORK errno=%d\n",errno);return;}
    if(!test){
        if(setgroups(0,NULL)||setgid(1000)||setuid(1000))_exit(120);
        struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
        struct __user_cap_data_struct c[2]={{0}};
        c[0].inheritable=c[0].effective=c[0].permitted=1U<<CAP_SYS_NICE;
        if(syscall(__NR_capset,&h,c))_exit(121);
        errno=0;int rc=setpriority(PRIO_PROCESS,getpid(),-8),saved=errno;
        fprintf(stderr,"UID1000_SETPRIORITY_PROBE uid=%u pid=%d nice=-8 rc=%d errno=%d seccomp=%d\n",
                (unsigned)getuid(),getpid(),rc,rc<0?saved:0,prctl(PR_GET_SECCOMP));
        _exit(rc<0?122:0);
    }
    int status=0;if(waitpid(test,&status,0)==test)
        fprintf(stderr,"UID1000_SETPRIORITY_PROBE_EXIT status=%d\n",status);
}
static void drain_property_requests(void){
    if(propfd<0)return;
    for(int count=0;count<8;count++){
        struct pollfd pf={propfd,POLLIN,0};if(poll(&pf,1,0)<=0)break;
        int fd=accept(propfd,NULL,NULL);if(fd<0)break;
        struct prop_msg msg;size_t have=0;
        while(have<sizeof(msg)){
            ssize_t n=recv(fd,(char*)&msg+have,sizeof(msg)-have,0);
            if(n<=0)break;have+=(size_t)n;
        }
        close(fd);
        if(have!=sizeof(msg) || msg.cmd!=PROP_MSG_SETPROP)continue;
        msg.name[PROP_NAME_MAX-1]=0;msg.value[PROP_VALUE_MAX-1]=0;
        size_t len=strlen(msg.name),vlen=strlen(msg.value);int legal=len>0;
        for(size_t i=0;i<len;i++)if(!((msg.name[i]>='a'&&msg.name[i]<='z')||
            (msg.name[i]>='A'&&msg.name[i]<='Z')||(msg.name[i]>='0'&&msg.name[i]<='9')||
            msg.name[i]=='.'||msg.name[i]=='_'||msg.name[i]=='-'))legal=0;
        if(!legal || vlen>=PROP_VALUE_MAX-1 || !strncmp(msg.name,"ctl.",4) ||
            !strcmp(msg.name,"sys.powerctl") || !strncmp(msg.name,"ro.",3))continue;
        const prop_info *pi=__system_property_find(msg.name);int rc;
        if(pi)rc=__system_property_update((prop_info*)pi,msg.value,vlen);
        else rc=__system_property_add(msg.name,len,msg.value,vlen);
        if(!rc)fprintf(stderr,"PROPERTY_SET %s=%s\n",msg.name,msg.value);
    }
}
static pid_t child=-1;
static void timeout_handler(int sig){if(child>0)kill(child,SIGKILL);_exit(124);}
static int sandbox(int zygote,int zygote_system_server,int system_server,int graphics,int apk_probe){
    if(prctl(PR_SET_SECUREBITS,SECBIT_NOROOT|SECBIT_NOROOT_LOCKED|
        SECBIT_NO_SETUID_FIXUP|SECBIT_NO_SETUID_FIXUP_LOCKED,0,0,0))return -1;
    struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
    struct __user_cap_data_struct c[2]={{0}};
    /* Zygote temporarily adopts uid/gid 9999 during static initializers.
     * Keep only the two capabilities required for that transition. */
    unsigned caps=((zygote || system_uid_probe)?((1U<<CAP_SETUID)|(1U<<CAP_SETGID)):0)|
        (zygote?((1U<<CAP_SYS_ADMIN)|(1U<<CAP_SETPCAP)):0)|
        (system_server?((1U<<CAP_SYS_NICE)|(1U<<CAP_CHOWN)):0)|
        (zygote_system_server?((1U<<CAP_IPC_LOCK)|(1U<<CAP_KILL)|(1U<<CAP_NET_ADMIN)|
            (1U<<CAP_NET_BIND_SERVICE)|(1U<<CAP_NET_BROADCAST)|(1U<<CAP_NET_RAW)|
            (1U<<CAP_SYS_MODULE)|(1U<<CAP_SYS_NICE)|(1U<<CAP_SYS_PTRACE)|(1U<<CAP_SYS_TIME)|
            (1U<<CAP_SYS_TTY_CONFIG)):0)|
        (graphics?((1U<<CAP_DAC_OVERRIDE)|(1U<<CAP_FOWNER)):0)|
        (apk_probe?((1U<<CAP_DAC_OVERRIDE)|(1U<<CAP_FOWNER)):0)|
        (netd_probe?((1U<<CAP_NET_ADMIN)|(1U<<CAP_NET_RAW)):0);
    if(vold_probe)caps|=(1U<<CAP_CHOWN)|(1U<<CAP_DAC_OVERRIDE)|(1U<<CAP_FOWNER);
    c[0].inheritable=c[0].effective=c[0].permitted=caps;
    if(syscall(__NR_capset,&h,c))return -1;
    /* Preserve only the UID transition and namespace-control capabilities
     * across the app_process exec. They are scoped to the private userns.
     * PR_CAP_AMBIENT / PR_CAP_AMBIENT_RAISE are absent from the old NDK headers. */
    if(zygote && (prctl(47,2,CAP_SETUID,0,0) || prctl(47,2,CAP_SETGID,0,0) ||
        prctl(47,2,CAP_SYS_ADMIN,0,0) || prctl(47,2,CAP_SETPCAP,0,0)))return -1;
    if(zygote_system_server){
        unsigned server_caps[]={CAP_IPC_LOCK,CAP_KILL,CAP_NET_ADMIN,CAP_NET_BIND_SERVICE,
            CAP_NET_BROADCAST,CAP_NET_RAW,CAP_SYS_MODULE,CAP_SYS_PTRACE,CAP_SYS_TIME,
            CAP_SYS_NICE,CAP_SYS_TTY_CONFIG};
        for(unsigned i=0;i<sizeof(server_caps)/sizeof(server_caps[0]);i++)
            if(prctl(47,2,server_caps[i],0,0))return -1;
    }
    if(system_server && prctl(47,2,CAP_SYS_NICE,0,0))return -1;
    if(system_server && prctl(47,2,CAP_CHOWN,0,0))return -1;
    if(graphics && (prctl(47,2,CAP_DAC_OVERRIDE,0,0) || prctl(47,2,CAP_FOWNER,0,0)))return -1;
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return -1;
    struct sock_filter code[64];unsigned n=0;
    code[n++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,arch));
    code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_ARM,1,0);
    code[n++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL);
    code[n++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr));
    if(zygote){
        /* Zygote apps may make a mount namespace. The explicit netd probe may
         * also create a private network namespace for its daemon child. */
        code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_unshare,0,netd_probe?4:3);
        code[n++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0]));
        code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,CLONE_NEWNS,netd_probe?2:1,0);
        if(netd_probe){
            code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,CLONE_NEWNET,1,0);
        }
        code[n++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
    }
    unsigned denied[]={
        __NR_reboot,__NR_swapon,__NR_swapoff,
        __NR_init_module,__NR_delete_module,__NR_ptrace,__NR_syslog,__NR_kexec_load,
        __NR_setns,__NR_finit_module,386,__NR_perf_event_open,__NR_open_by_handle_at};
    for(unsigned i=0;i<sizeof(denied)/sizeof(denied[0]);i++){
        code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,denied[i],0,1);
        code[n++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
    }
    if(!zygote){
        unsigned denied_ns[]={__NR_mount,__NR_unshare,__NR_umount2};
        for(unsigned i=0;i<sizeof(denied_ns)/sizeof(denied_ns[0]);i++){
            code[n++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,denied_ns[i],0,1);
            code[n++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
        }
    }
    code[n++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    struct sock_fprog p={(unsigned short)n,code};
    return prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&p);
}
int main(int argc, char **argv){
    char marker[64]={};FILE *f=fopen("/art.marker","r");if(f){fgets(marker,sizeof(marker),f);fclose(f);}
    if(getpid()!=1 || strcmp(marker,"kindle-art-lab\n"))return 90;
    setbuf(stderr,NULL);signal(SIGALRM,timeout_handler);signal(SIGTERM,timeout_handler);alarm(22);
    int zygote_system_server=argc==2 && !strcmp(argv[1],"--zygote-system-server-probe");
    int apk_probe=getenv("KINDLE_APK_PROBE") && !strcmp(getenv("KINDLE_APK_PROBE"),"1");
    int zygote_headless_sf=zygote_system_server && getenv("KINDLE_ZYGOTE_HEADLESS_SF") &&
        !strcmp(getenv("KINDLE_ZYGOTE_HEADLESS_SF"),"1");
    // Private Android data: allow system UID's group to reach the existing
    // shared controls directory without granting directory listing to apps.
    if (zygote_headless_sf && (chown("/data/local",0,1000) || chmod("/data/local",0710))) {
        fprintf(stderr,"ANDROID_LOCAL_TRAVERSE_SETUP_FAILED errno=%d\n",errno);
        return 96;
    }
    if (zygote_headless_sf) {
        unlink("/data/local/tmp/kindle-page-direction");
        unlink("/data/local/tmp/kindle-page-direction.new");
    }
    int zygote=(argc==2 && !strcmp(argv[1],"--zygote-probe")) || zygote_system_server;
    int graphics=argc==2 && !strcmp(argv[1],"--system-server-graphics-probe");
    netd_probe=(graphics || zygote_headless_sf) && getenv("KINDLE_NETD_PROBE") && !strcmp(getenv("KINDLE_NETD_PROBE"),"1");
    vold_probe=netd_probe && getenv("KINDLE_VOLD_PROBE") && !strcmp(getenv("KINDLE_VOLD_PROBE"),"1");
    audio_probe=(vold_probe || zygote_headless_sf) && getenv("KINDLE_AUDIO_PROBE") && !strcmp(getenv("KINDLE_AUDIO_PROBE"),"1");
    system_uid_probe=audio_probe && getenv("KINDLE_SYSTEM_UID") && !strcmp(getenv("KINDLE_SYSTEM_UID"),"1000");
    keystore_probe=zygote_system_server && getenv("KINDLE_KEYSTORE_PROBE") &&
        !strcmp(getenv("KINDLE_KEYSTORE_PROBE"),"1");
    int system_server=graphics || zygote_system_server || (argc==2 && !strcmp(argv[1],"--system-server-probe"));
    int android_session=getenv("KINDLE_ANDROID_SESSION") && !strcmp(getenv("KINDLE_ANDROID_SESSION"),"1");
    if(zygote || system_server)alarm(android_session?0:(apk_probe?570:40));
    if(zygote || system_server){
        if(properties(android_session))return 94;
        logger=unix_socket("/dev/socket/logdw",SOCK_DGRAM);
        if(logger<0)return 95;
        if(graphics){int size=262144;setsockopt(logger,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size));}
        chmod("/dev/socket/logdw",0666);
    }
    if(zygote){
        zsocket=unix_socket("/dev/socket/zygote",SOCK_STREAM);
        if(zsocket<0 || listen(zsocket,4))return 96;
        if(chmod("/dev/socket/zygote",0666))return 96;
        fprintf(stderr,"ZYGOTE_SOCKET_MODE=0666 private-container\n");
        char value[16];snprintf(value,sizeof(value),"%d",zsocket);setenv("ANDROID_SOCKET_zygote",value,1);
    }
    if(system_server){
        installdfd=unix_socket("/dev/socket/installd",SOCK_STREAM);
        if(installdfd<0 || listen(installdfd,8))return 96;
        chmod("/dev/socket/installd",0666);
    }
    if(netd_probe){
        char path[80];
        for(unsigned i=0;i<4;i++){
            snprintf(path,sizeof(path),"/dev/socket/%s",netd_socket_names[i]);
            netd_sockets[i]=unix_socket(path,SOCK_STREAM);
            if(netd_sockets[i]<0 || fcntl(netd_sockets[i],F_SETFD,FD_CLOEXEC))return 96;
            gid_t group=(!strcmp(netd_socket_names[i],"dnsproxyd")||!strcmp(netd_socket_names[i],"fwmarkd"))?3003:1000;
            if(chown(path,0,group) || chmod(path,0660))return 96;
        }
    }
    if(vold_probe){
        const char *paths[]={"/dev/socket/vold","/dev/socket/cryptd"};
        for(unsigned i=0;i<2;i++){
            vold_sockets[i]=unix_socket(paths[i],SOCK_STREAM);
            if(vold_sockets[i]<0 || fcntl(vold_sockets[i],F_SETFD,FD_CLOEXEC))return 96;
            if(chown(paths[i],0,1000) || chmod(paths[i],0660))return 96;
        }
    }
    if(android_session){
        lmkdfd=unix_socket("/dev/socket/lmkd",SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(lmkdfd<0 || listen(lmkdfd,2) || chown("/dev/socket/lmkd",0,1000) || chmod("/dev/socket/lmkd",0660))return 96;
        fprintf(stderr,"LMKD_COMPAT_READY=1 policy=kernel-oom no-host-process-control\n");
    }
    if(sandbox(zygote,zygote_system_server,system_server,graphics,apk_probe)){perror("art sandbox");return 91;}
    fprintf(stderr,"ART_GUARD pid=1 seccomp=%d no_new_privs=%d ambient_setuid=%d ambient_setgid=%d\n",prctl(PR_GET_SECCOMP),prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0),prctl(47,1,CAP_SETUID,0,0),prctl(47,1,CAP_SETGID,0,0));
    pid_t fuse_broker=-1;
    if(android_session && vold_probe){
        fuse_broker=start_fuse_mount_broker();
        if(fuse_broker<0)return 98;
    }
    if(zygote_system_server)probe_uid1000_setpriority();
    if(system_server && !zygote_system_server){
        errno=0;int rc=chown("/data/misc/user/0",1000,9997);int e=errno;
        fprintf(stderr,"PARENT_CHOWN_TEST rc=%d errno=%d\n",rc,e);
        if(!rc && chown("/data/misc/user/0",0,0))fprintf(stderr,"PARENT_CHOWN_RESTORE errno=%d\n",errno);
    }
    pid_t manager=-1;
    if(system_server){
        manager=fork();if(manager<0)return 92;
        if(!manager){
            if(logger>=0)close(logger);
            if(installdfd>=0)close(installdfd);
            if(propfd>=0)close(propfd);
            struct __user_cap_data_struct daemon_caps[2]={{0}};
            struct __user_cap_header_struct header={_LINUX_CAPABILITY_VERSION_3,0};
            daemon_caps[0].inheritable=daemon_caps[0].effective=daemon_caps[0].permitted=1U<<CAP_CHOWN;
            if(syscall(__NR_capset,&header,daemon_caps)){perror("installd capset");_exit(125);}
            prctl(47,4,0,0,0);
            if(prctl(47,2,CAP_CHOWN,0,0)){perror("installd ambient chown");_exit(125);}
            if(zygote_system_server){
                fprintf(stderr,"INSTALLD_PREEXEC_CHOWN_SKIPPED=preowned\n");
            }else{
                errno=0;int child_chown=chown("/data/misc/user/0",1000,9997);int child_errno=errno;
                if(!child_chown && chown("/data/misc/user/0",0,0))fprintf(stderr,"INSTALLD_PREEXEC_RESTORE errno=%d\n",errno);
                fprintf(stderr,"INSTALLD_PREEXEC_CHOWN rc=%d errno=%d\n",child_chown,child_errno);
            }
            struct __user_cap_data_struct verify[2]={{0}};
            if(syscall(__NR_capget,&header,verify))perror("installd capget");
            fprintf(stderr,"INSTALLD_PREEXEC uid=%u euid=%u chown_eff=%u chown_prm=%u chown_inh=%u chown_amb=%d nnp=%d\n",
                (unsigned)getuid(),(unsigned)geteuid(),!!(verify[0].effective&(1U<<CAP_CHOWN)),
                !!(verify[0].permitted&(1U<<CAP_CHOWN)),!!(verify[0].inheritable&(1U<<CAP_CHOWN)),
                prctl(47,1,CAP_CHOWN,0,0),prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0));
            execl("/system/bin/servicemanager-bionic","servicemanager-bionic",(char*)NULL);
            perror("servicemanager exec");_exit(127);
        }
        usleep(300000);
    }
    pid_t compositor=-1;
    if(graphics||zygote_headless_sf){
        compositor=fork();if(compositor<0)return 92;
        if(!compositor){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            struct __user_cap_header_struct sf_header={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct sf_caps[2]={{0}};
            sf_caps[0].inheritable=sf_caps[0].effective=sf_caps[0].permitted=1U<<CAP_SYS_NICE;
            if(syscall(__NR_capset,&sf_header,sf_caps))_exit(125);
            if(prctl(47,4,0,0,0) || prctl(47,2,CAP_SYS_NICE,0,0))_exit(125);
            setenv("LD_PRELOAD","/system/lib/libheadlesslog.so:/system/lib/libkindleashmem.so",1);
            execl("/system/bin/surfaceflinger-headless","surfaceflinger-headless",(char*)NULL);
            perror("SurfaceFlinger exec");_exit(127);
        }
        int sf_ready=0;
        for(int i=0;i<30;i++){
            int st;
            if(waitpid(compositor,&st,WNOHANG)==compositor){
                fprintf(stderr,"GRAPHICS_SF_EARLY_EXIT=%d\n",st);return 99;
            }
            pid_t query=fork();if(query<0)return 92;
            if(!query){execl("/system/bin/sf-service-check","sf-service-check",(char*)NULL);_exit(127);}
            if(waitpid(query,&st,0)==query && WIFEXITED(st) && WEXITSTATUS(st)==0){sf_ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!sf_ready){fprintf(stderr,"GRAPHICS_SF_REGISTRATION_FAILED\n");return 99;}
        fprintf(stderr,"GRAPHICS_SF_READY pid=%d\n",compositor);
    }
    pid_t virtual_battery=-1;
    if(graphics||zygote_headless_sf){
        virtual_battery=fork();if(virtual_battery<0)return 92;
        if(!virtual_battery){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            struct __user_cap_header_struct header={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct caps[2]={{0}};
            if(syscall(__NR_capset,&header,caps) || prctl(47,4,0,0,0))_exit(125);
            execl("/system/bin/headless-battery","headless-battery",(char*)NULL);_exit(127);
        }
        int ready=0;
        for(int i=0;i<30;i++){
            if(check_service("batteryproperties")==0){ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!ready){fprintf(stderr,"GRAPHICS_BATTERY_REGISTRATION_FAILED\n");return 99;}
        if(zygote_headless_sf)fprintf(stderr,"HEADLESS_BATTERY_READY=1\n");
    }
    pid_t netd=-1;
    if(netd_probe){
        netd=fork();if(netd<0)return 92;
        if(!netd){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct c[2]={{0}};
            c[0].inheritable=c[0].effective=c[0].permitted=(1U<<CAP_CHOWN)|(1U<<CAP_NET_ADMIN)|(1U<<CAP_NET_RAW)|(1U<<CAP_NET_BIND_SERVICE);
            if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0) ||
               prctl(47,2,CAP_NET_ADMIN,0,0) || prctl(47,2,CAP_NET_RAW,0,0) ||
               prctl(47,2,CAP_NET_BIND_SERVICE,0,0) || prctl(47,2,CAP_CHOWN,0,0))_exit(125);
            for(unsigned i=0;i<4;i++){
                char key[80],value[16];
                if(fcntl(netd_sockets[i],F_SETFD,0))_exit(125);
                snprintf(key,sizeof(key),"ANDROID_SOCKET_%s",netd_socket_names[i]);
                snprintf(value,sizeof(value),"%d",netd_sockets[i]);setenv(key,value,1);
            }
            setenv("LD_PRELOAD","/system/lib/libnetd-exit-trace.so:/system/lib/libkindleashmem.so",1);
            fprintf(stderr,"NETD_SESSION_NETNS_START caps=CHOWN,NET_ADMIN,NET_RAW,NET_BIND_SERVICE net=isolated-session\n");
            fprintf(stderr,"ISOLATED_NETWORK_TOOL_NOOP_SHIMS_ACTIVE=1\n");
            execl("/system/bin/netd","netd",(char*)NULL);perror("netd exec");_exit(127);
        }
        int ready=0;
        for(int i=0;i<30;i++){
            int st;if(waitpid(netd,&st,WNOHANG)==netd){drain_logs();fprintf(stderr,"NETD_EARLY_EXIT=%d\n",st);return 99;}
            if(check_service("netd")==0){ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!ready){drain_logs();fprintf(stderr,"NETD_REGISTRATION_FAILED\n");return 99;}
        fprintf(stderr,"NETD_READY pid=%d\n",netd);
    }
    pid_t vold=-1;
    if(vold_probe){
        vold=fork();if(vold<0)return 92;
        if(!vold){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct c[2]={{0}};
            unsigned caps[]={CAP_NET_ADMIN,CAP_CHOWN,CAP_DAC_OVERRIDE,CAP_FOWNER,CAP_SETGID,CAP_SETUID};
            for(unsigned i=0;i<sizeof(caps)/sizeof(caps[0]);i++)c[0].permitted|=1U<<caps[i];
            c[0].inheritable=c[0].effective=c[0].permitted;
            if(syscall(__NR_capset,&h,c)){
                fprintf(stderr,"VOLD_CAPSET_FAILED errno=%d (%s)\n",errno,strerror(errno));_exit(125);
            }
            if(prctl(47,4,0,0,0)){
                fprintf(stderr,"VOLD_AMBIENT_CLEAR_FAILED errno=%d (%s)\n",errno,strerror(errno));_exit(125);
            }
            for(unsigned i=0;i<sizeof(caps)/sizeof(caps[0]);i++)if(prctl(47,2,caps[i],0,0)){
                fprintf(stderr,"VOLD_AMBIENT_RAISE_FAILED cap=%u errno=%d (%s)\n",caps[i],errno,strerror(errno));_exit(125);
            }
            const char *names[]={"vold","cryptd"};
            for(unsigned i=0;i<2;i++){
                char key[80],value[16];
                if(fcntl(vold_sockets[i],F_SETFD,0))_exit(125);
                snprintf(key,sizeof(key),"ANDROID_SOCKET_%s",names[i]);
                snprintf(value,sizeof(value),"%d",vold_sockets[i]);setenv(key,value,1);
            }
            if(android_session)setenv("KINDLELAB_FUSE_BROKER","1",1);
            setenv("LD_PRELOAD","/system/lib/libnetd-exit-trace.so:/system/lib/libkindleashmem.so",1);
            fprintf(stderr,"VOLD_TMPFS_START mount_denied=1 block_devices=0 sysfs=0\n");
            execl("/system/bin/vold","vold","--blkid_context=u:r:blkid:s0",
                  "--blkid_untrusted_context=u:r:blkid_untrusted:s0",
                  "--fsck_context=u:r:fsck:s0","--fsck_untrusted_context=u:r:fsck_untrusted:s0",(char*)NULL);
            perror("vold exec");_exit(127);
        }
        int ready=0;
        int wait_attempts=android_session?1000:100;
        for(int i=0;i<wait_attempts;i++){
            int st;if(waitpid(vold,&st,WNOHANG)==vold){drain_logs();fprintf(stderr,"VOLD_EARLY_EXIT=%d\n",st);return 99;}
            int first=0,second=0;socklen_t len=sizeof(int);
            if(!getsockopt(vold_sockets[0],SOL_SOCKET,SO_ACCEPTCONN,&first,&len) &&
               !getsockopt(vold_sockets[1],SOL_SOCKET,SO_ACCEPTCONN,&second,&len) && first && second){ready=1;break;}
            drain_logs();drain_property_requests();usleep(20000);
        }
        if(!ready){
            int first=-1,second=-1;socklen_t len=sizeof(int);
            int first_rc=getsockopt(vold_sockets[0],SOL_SOCKET,SO_ACCEPTCONN,&first,&len);
            len=sizeof(int);int second_rc=getsockopt(vold_sockets[1],SOL_SOCKET,SO_ACCEPTCONN,&second,&len);
            int st=0;pid_t child_state=waitpid(vold,&st,WNOHANG);
            drain_logs();fprintf(stderr,"VOLD_LISTEN_FAILED first=%d/%d second=%d/%d child_state=%d status=%d\n",
                                  first_rc,first,second_rc,second,(int)child_state,st);
            return 99;
        }
        fprintf(stderr,"VOLD_READY pid=%d sockets=vold,cryptd\n",vold);
    }
    pid_t audioserver=-1;
    if(audio_probe){
        audioserver=fork();if(audioserver<0)return 92;
        if(!audioserver){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct c[2]={{0}};
            c[0].inheritable=c[0].effective=c[0].permitted=1U<<CAP_SYS_NICE;
            if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0) || prctl(47,2,CAP_SYS_NICE,0,0))_exit(125);
            setenv("LD_PRELOAD","/system/lib/libkindleashmem.so",1);
            fprintf(stderr,"AUDIO_NULL_START physical_devices=0\n");
            execl("/system/bin/audioserver","audioserver",(char*)NULL);perror("audioserver exec");_exit(127);
        }
        int ready=0;
        for(int i=0;i<30;i++){
            int st;if(waitpid(audioserver,&st,WNOHANG)==audioserver){drain_logs();fprintf(stderr,"AUDIO_EARLY_EXIT=%d\n",st);return 99;}
            if(check_service("media.audio_flinger")==0 && check_service("media.audio_policy")==0){ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!ready){drain_logs();fprintf(stderr,"AUDIO_REGISTRATION_FAILED\n");return 99;}
        fprintf(stderr,"AUDIO_READY pid=%d backend=null\n",audioserver);
    }
    if(android_session){
        const char *media_daemons[]={"/system/bin/mediacodec","/system/bin/mediaserver"};
        for(unsigned index=0;index<2;index++){
            if(access(media_daemons[index],X_OK)!=0)continue;
            pid_t media=fork();if(media<0)return 92;
            if(!media){
                if(logger>=0)close(logger);
                if(propfd>=0)close(propfd);
                if(installdfd>=0)close(installdfd);
                struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
                struct __user_cap_data_struct c[2]={{0}};
                if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0))_exit(125);
                setenv("LD_PRELOAD","/system/lib/libkindleashmem.so",1);
                execl(media_daemons[index],strrchr(media_daemons[index],'/')+1,(char*)NULL);
                perror("media daemon exec");_exit(127);
            }
            fprintf(stderr,"ANDROID_MEDIA_DAEMON_STARTED path=%s pid=%d\n",media_daemons[index],media);
        }
    }
    pid_t keystore=-1;
    if(keystore_probe){
        struct stat keystore_dir={0};
        if(stat("/data/misc/keystore",&keystore_dir) || keystore_dir.st_uid!=1017 ||
           keystore_dir.st_gid!=1017 || (keystore_dir.st_mode&0777)!=0700){
            fprintf(stderr,"KEYSTORE_DATA_DIR_NOT_PREOWNED errno=%d uid=%u gid=%u mode=%o\n",
                    errno,(unsigned)keystore_dir.st_uid,(unsigned)keystore_dir.st_gid,
                    (unsigned)(keystore_dir.st_mode&0777));
            return 96;
        }
        keystore=fork();if(keystore<0)return 92;
        if(!keystore){
            if(logger>=0)close(logger);if(propfd>=0)close(propfd);if(installdfd>=0)close(installdfd);
            if(setgroups(0,NULL) || setgid(1017) || setuid(1017))_exit(125);
            struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct c[2]={{0}};
            if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0))_exit(125);
            setenv("LD_PRELOAD","/system/lib/libkindleashmem.so",1);
            fprintf(stderr,"KEYSTORE_TMPFS_START uid=%u gid=%u caps=0 hardware_devices=0\n",(unsigned)getuid(),(unsigned)getgid());
            execl("/system/bin/keystore","keystore","/data/misc/keystore",(char*)NULL);perror("keystore exec");_exit(127);
        }
        int ready=0;
        for(int i=0;i<30;i++){
            int st;if(waitpid(keystore,&st,WNOHANG)==keystore){drain_logs();fprintf(stderr,"KEYSTORE_EARLY_EXIT=%d\n",st);return 99;}
            if(check_service("android.security.keystore")==0){ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!ready){drain_logs();fprintf(stderr,"KEYSTORE_REGISTRATION_FAILED\n");return 99;}
        fprintf(stderr,"KEYSTORE_READY pid=%d storage=tmpfs\n",keystore);
    }
    if(apk_probe){
        textservices_stub=fork();if(textservices_stub<0)return 92;
        if(!textservices_stub){
            if(logger>=0)close(logger);if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);
            setenv("CLASSPATH","/tests/textservices-null-stub.dex",1);
            char *args[]={"app_process32","/system/bin","org.kindlelab.textservices.NullTextServices",NULL};
            fprintf(stderr,"TEXTSERVICES_NULL_STUB_START=1\n");
            execv("/system/bin/app_process32",args);perror("TextServices stub exec");_exit(127);
        }
        int ready=0;
        for(int i=0;i<30;i++){
            int st;if(waitpid(textservices_stub,&st,WNOHANG)==textservices_stub){
                drain_logs();fprintf(stderr,"TEXTSERVICES_NULL_STUB_EARLY_EXIT=%d\n",st);return 99;
            }
            if(check_service("textservices")==0){ready=1;break;}
            drain_logs();drain_property_requests();usleep(100000);
        }
        if(!ready){drain_logs();fprintf(stderr,"TEXTSERVICES_NULL_STUB_REGISTRATION_FAILED\n");return 99;}
        fprintf(stderr,"TEXTSERVICES_NULL_STUB_READY pid=%d contract=no-spell-checker\n",textservices_stub);
    }
    pid_t installd=-1;
    if(system_server){
        installd=fork();if(installd<0)return 92;
        if(!installd){
            if(logger>=0)close(logger);
            if(propfd>=0)close(propfd);
            if(zsocket>=0)close(zsocket);
            struct __user_cap_data_struct daemon_caps[2]={{0}};
            struct __user_cap_header_struct header={_LINUX_CAPABILITY_VERSION_3,0};
            /* Android dexopt switches to a disposable app uid/gid. Keep
             * ownership and identity-transition capabilities on installd only. */
            daemon_caps[0].inheritable=daemon_caps[0].effective=daemon_caps[0].permitted=1U<<CAP_CHOWN;
            if(graphics || apk_probe)daemon_caps[0].inheritable=daemon_caps[0].effective=daemon_caps[0].permitted|=
                (1U<<CAP_DAC_OVERRIDE)|(1U<<CAP_FOWNER)|(1U<<CAP_SETUID)|(1U<<CAP_SETGID);
            if(syscall(__NR_capset,&header,daemon_caps)){perror("installd capset");_exit(125);}
            prctl(47,4,0,0,0);
            int flags=fcntl(installdfd,F_GETFD);if(flags<0 || fcntl(installdfd,F_SETFD,flags&~FD_CLOEXEC)){perror("installd socket fd");_exit(125);}
            if((graphics || apk_probe) && (prctl(47,2,CAP_CHOWN,0,0) || prctl(47,2,CAP_DAC_OVERRIDE,0,0) ||
                prctl(47,2,CAP_FOWNER,0,0) || prctl(47,2,CAP_SETUID,0,0) || prctl(47,2,CAP_SETGID,0,0)))_exit(125);
            char fdstr[16];snprintf(fdstr,sizeof(fdstr),"%d",installdfd);
            setenv("ANDROID_SOCKET_installd",fdstr,1);
            if(graphics||zygote_system_server){
                setenv("LD_PRELOAD",(apk_probe && !android_session)?
                       "/system/lib/libkindle-userxattr-compat.so:/system/lib/libkindleashmem.so":
                       "/system/lib/libkindleashmem.so",1);
                if(zygote_system_server)fprintf(stderr,"SYSTEM_SERVER_INSTALLD_REAL=1\n");
            }else setenv("LD_PRELOAD","/system/lib/libinstalld-probe.so:/system/lib/libkindleashmem.so",1);
            execl("/system/bin/installd","installd",(char*)NULL);
            perror("installd exec");_exit(127);
        }
        /* Capture the daemon's post-exec credentials before directory setup
         * exits, without attaching a debugger or changing its behavior. */
        int installd_exec_confirmed=0;
        for(int i=0;i<1000;i++){
            int st;pid_t exited=waitpid(installd,&st,WNOHANG);
            if(exited==installd){
                if(WIFEXITED(st))fprintf(stderr,"INSTALLD_EARLY_EXIT=1 code=%d\n",WEXITSTATUS(st));
                else if(WIFSIGNALED(st))fprintf(stderr,"INSTALLD_EARLY_EXIT=1 signal=%d\n",WTERMSIG(st));
                installd=-1;break;
            }
            if(graphics || zygote_system_server){
                char executable[256]={0},exe_path[64];
                snprintf(exe_path,sizeof(exe_path),"/proc/%d/exe",installd);
                ssize_t n=readlink(exe_path,executable,sizeof(executable)-1);
                if(n<0 || !strstr(executable,"/system/bin/installd")){usleep(1000);continue;}
            }
            char path[64],line[256];snprintf(path,sizeof(path),"/proc/%d/status",installd);
            FILE *status=fopen(path,"r");int uid=0;unsigned long long eff=0,prm=0,inh=0,amb=0;
            if(status){while(fgets(line,sizeof(line),status)){
                if(sscanf(line,"Uid: %d",&uid)==1)continue;
                if(sscanf(line,"CapEff: %llx",&eff)==1)continue;
                if(sscanf(line,"CapPrm: %llx",&prm)==1)continue;
                if(sscanf(line,"CapInh: %llx",&inh)==1)continue;
                if(sscanf(line,"CapAmb: %llx",&amb)==1)continue;
            }fclose(status);fprintf(stderr,"INSTALLD_POSTEXEC pid=%d uid=%d eff=%llx prm=%llx inh=%llx amb=%llx exe_confirmed=%d\n",installd,uid,eff,prm,inh,amb,graphics||zygote_system_server);installd_exec_confirmed=1;break;}
            usleep(1000);
        }
        if(zygote_system_server && !installd_exec_confirmed){
            fprintf(stderr,"INSTALLD_EXEC_NOT_CONFIRMED=1; refusing to start Zygote with a dead installer\n");
            return 97;
        }
        usleep(100000);
    }
    if(zygote){
        pid_t java_smoke=fork();if(java_smoke<0)return 92;
        if(!java_smoke){
            char *args[]={"dalvikvm32","-Xnorelocate","-Xnoimage-dex2oat","-Xnodex2oat",
                "-Xusejit:false","-Xint","-Xms8m","-Xmx32m","-Ximage:/system/framework/boot.art",
                "-cp","/tests/Hello.dex","Hello","zygote",NULL};
            execv("/system/bin/dalvikvm32",args);perror("zygote Java smoke exec");_exit(127);
        }
        int smoke_status;
        if(waitpid(java_smoke,&smoke_status,0)!=java_smoke || !WIFEXITED(smoke_status) || WEXITSTATUS(smoke_status)!=0){
            fprintf(stderr,"ZYGOTE_JAVA_SMOKE_FAILED status=%d\n",smoke_status);return 95;
        }
        zygote_hello_result=1;
        fprintf(stderr,"ZYGOTE_JAVA_SMOKE_PASSED\n");
    }
    if(zygote_system_server)fprintf(stderr,"ZYGOTE_SYSTEM_SERVER_REQUESTED=1\n");
    child=fork();if(child<0)return 92;
    if(!child){
        if(logger>=0)close(logger);
        if(propfd>=0)close(propfd);
        if(installdfd>=0)close(installdfd);
        /* Zygote consumes this already-listening socket via
         * ANDROID_SOCKET_zygote; only the non-Zygote probes close it. */
        if(zsocket>=0 && !zygote)close(zsocket);
        if(zygote){
            for(int fd=STDIN_FILENO;fd<=STDERR_FILENO;fd++)
                if(close(fd)<0 && errno!=EBADF){perror("zygote stdio close");_exit(127);}
            const char *preload=getenv("LD_PRELOAD");char preload_chain[512];
            if(android_session){
                snprintf(preload_chain,sizeof(preload_chain),"/system/lib/libegl-software-properties.so:/system/lib/libgetsockname-trace.so:%s",preload?preload:"");
            }else
            if(preload && *preload)
                snprintf(preload_chain,sizeof(preload_chain),"/system/lib/libgetsockname-trace.so:%s",preload);
            else
                snprintf(preload_chain,sizeof(preload_chain),"/system/lib/libgetsockname-trace.so");
            setenv("LD_PRELOAD",preload_chain,1);
            setenv("KINDLE_FDTRACE_PATH","/data/local/tmp/getsockname-trace.log",1);
            char *zargs[]={"app_process32","-Xzygote","-Xrelocate","-Ximage-dex2oat","-Xnodex2oat",
                android_session?"-Xusejit:true":"-Xusejit:false",
                android_session?"-Xms16m":"-Xms8m",android_session?"-Xmx128m":"-Xmx32m",
                android_session?"/system/bin":"-Xint",android_session?"--zygote":"/system/bin",
                android_session?(zygote_system_server?"--start-system-server":"--socket-name=zygote"):"--zygote",
                android_session?(zygote_system_server?"--socket-name=zygote":NULL):(zygote_system_server?"--start-system-server":NULL),
                android_session?NULL:"--socket-name=zygote",NULL};
            execv("/system/bin/app_process32",zargs);perror("zygote exec");_exit(127);
        }
        if(argc==2 && !strcmp(argv[1],"--app-process-smoke")){
            char *smoke[]={"app_process32",NULL};
            execv("/system/bin/app_process32",smoke);perror("app_process exec");_exit(127);
        }
        if(system_server){
            if(system_uid_probe){
                gid_t groups[]={1000,1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1018,1021,1032,3001,3002,3003,3006,3007,3009,3010};
                if(setgroups(sizeof(groups)/sizeof(groups[0]),groups) || setgid(1000) || setuid(1000)){
                    perror("SystemServer identity transition");_exit(126);
                }
            }
            struct __user_cap_data_struct server_caps[2]={{0}};
            struct __user_cap_header_struct header={_LINUX_CAPABILITY_VERSION_3,0};
            server_caps[0].inheritable=server_caps[0].effective=server_caps[0].permitted=1U<<CAP_SYS_NICE;
            if(graphics)server_caps[0].inheritable=server_caps[0].effective=server_caps[0].permitted|=(1U<<CAP_CHOWN)|(1U<<CAP_DAC_OVERRIDE)|(1U<<CAP_FOWNER);
            if(syscall(__NR_capset,&header,server_caps))_exit(126);
            if(system_uid_probe){
                if(prctl(47,4,0,0,0))_exit(126);
                unsigned keep[]={CAP_SYS_NICE,CAP_CHOWN,CAP_DAC_OVERRIDE,CAP_FOWNER};
                for(unsigned i=0;i<4;i++)if(prctl(47,2,keep[i],0,0))_exit(126);
                fprintf(stderr,"SYSTEM_SERVER_IDENTITY uid=%u gid=%u seccomp=%d nnp=%d\n",(unsigned)getuid(),(unsigned)getgid(),prctl(PR_GET_SECCOMP),prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0));
            }
            if(!graphics)prctl(47,3,CAP_CHOWN,0,0);
            if(graphics)setenv("LD_PRELOAD","/system/lib/libkindleashmem.so",1);
            if(system_uid_probe)setenv("LD_PRELOAD","/system/lib/libegl-software-properties.so:/system/lib/libkindleashmem.so",1);
            fprintf(stderr,"SYSTEM_SERVER_UID=%u capability_mask=%x\n",(unsigned)getuid(),server_caps[0].effective);
            char *server[]={"app_process32","-Xnorelocate","-Xnoimage-dex2oat","-Xnodex2oat",
                "-Xusejit:false","-Xint","-Xms16m","-Xmx96m","/system/bin","--application",
                "com.android.server.SystemServer",NULL};
            setenv("CLASSPATH","/system/framework/services.jar",1);
            if(graphics){
                server[10]="HeadlessSystemServer";
                setenv("CLASSPATH","/tests/SystemServerBootstrap.dex",1);
            }
            execv("/system/bin/app_process32",server);perror("SystemServer exec");_exit(127);
        }
        char *args[]={"dalvikvm32","-Xnorelocate","-Xnoimage-dex2oat","-Xnodex2oat",
            "-Xusejit:false","-Xint","-Xms8m","-Xmx32m","-Ximage:/system/framework/boot.art",
            "-cp","/tests/Hello.dex","Hello",NULL};
        execv("/system/bin/dalvikvm32",args);perror("dalvik exec");_exit(127);
    }
    int status,query=-1,ready=0,fork_sent=0,fork_response=0,child_identity=-2,zygote_shutdown_sent=0;
    uint32_t fork_pid_wire=0;pid_t fork_pid=-1;int fork_wrapper=0;
    unsigned char fork_response_data[5];size_t fork_response_have=0;
    struct timespec fork_request_time={0};int fork_wait_reported=0;
    if(zygote){
        query=socket(AF_UNIX,SOCK_STREAM,0);
        struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/dev/socket/zygote");
        if(query<0 || connect(query,(struct sockaddr*)&a,sizeof(a)))return 97;
        const char request[]="1\n--query-abi-list\n";
        if(write(query,request,sizeof(request)-1)!=sizeof(request)-1)return 98;
    }
    pid_t apk_runner=-1;
    if(apk_probe){
        apk_runner=fork();if(apk_runner<0)return 92;
        if(!apk_runner){
            if(logger>=0)close(logger);if(propfd>=0)close(propfd);
            if(installdfd>=0)close(installdfd);if(zsocket>=0)close(zsocket);
            if(query>=0)close(query);
            struct __user_cap_header_struct h={_LINUX_CAPABILITY_VERSION_3,0};
            struct __user_cap_data_struct c[2]={{0}};
            unsigned runner_caps[]={CAP_CHOWN,CAP_DAC_OVERRIDE,CAP_FOWNER,CAP_SETGID,CAP_SETUID};
            for(unsigned i=0;i<sizeof(runner_caps)/sizeof(runner_caps[0]);i++){
                unsigned cap=runner_caps[i];c[cap/32].effective|=1U<<(cap%32);
                c[cap/32].permitted|=1U<<(cap%32);c[cap/32].inheritable|=1U<<(cap%32);
            }
            if(syscall(__NR_capset,&h,c) || prctl(47,4,0,0,0)){
                fprintf(stderr,"APK_RUNNER_CAP_REDUCTION_FAILED errno=%d\n",errno);_exit(125);
            }
            for(unsigned i=0;i<sizeof(runner_caps)/sizeof(runner_caps[0]);i++)
                if(prctl(47,2,runner_caps[i],0,0)){
                    fprintf(stderr,"APK_RUNNER_AMBIENT_CAP_FAILED cap=%u errno=%d\n",runner_caps[i],errno);_exit(125);
                }
            fprintf(stderr,"APK_RUNNER_CAPS_RESTRICTED=chown,dac_override,fowner,setgid,setuid sys_admin=0\n");
            execl("/system/bin/apk-probe-runner","apk-probe-runner",(char*)NULL);
            perror("APK probe runner exec");_exit(127);
        }
        fprintf(stderr,"APK_PROBE_RUNNER_FORKED pid=%d pid1_preserved=1\n",apk_runner);
    }
    struct timespec probe_start;clock_gettime(CLOCK_MONOTONIC,&probe_start);
    int service_snapshot=0;
    while(1){
        pid_t w=waitpid(child,&status,WNOHANG);
        if(apk_runner>0){
            int apk_status=0;pid_t apk_wait=waitpid(apk_runner,&apk_status,WNOHANG);
            if(apk_wait==apk_runner){
                if(WIFEXITED(apk_status)){
                    fprintf(stderr,"APK_PROBE_RUNNER_EXIT=%d\n",WEXITSTATUS(apk_status));
                    if(WEXITSTATUS(apk_status)==0){fprintf(stderr,"APK_PROBE_COMPLETE=1 pid1_shutdown=namespace_exit\n");return 0;}
                    return WEXITSTATUS(apk_status);
                }
                if(WIFSIGNALED(apk_status))fprintf(stderr,"APK_PROBE_RUNNER_SIGNAL=%d\n",WTERMSIG(apk_status));
                return 127;
            }
            if(apk_wait<0 && errno!=EINTR){fprintf(stderr,"APK_PROBE_RUNNER_WAIT_FAILED errno=%d\n",errno);return 93;}
        }
        if(zygote_system_server)report_system_server_caps();
        drain_logs();drain_property_requests();
        if(apk_probe && system_server_phase550_seen && system_server_pid>0 && !system_server_trace_dumped){
            if(kill(system_server_pid,SIGQUIT)==0){
                fprintf(stderr,"APK_SYSTEM_SERVER_SIGQUIT_SENT pid=%d phase=550\n",system_server_pid);
                report_system_server_java_traces(system_server_pid);
            }else fprintf(stderr,"APK_SYSTEM_SERVER_SIGQUIT_FAILED pid=%d errno=%d\n",system_server_pid,errno);
            system_server_trace_dumped=1;
        }
        if(graphics && !service_snapshot){
            struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
            if(now.tv_sec-probe_start.tv_sec>=20){
                const char *services[]={"activity","package","window","input","display","power","battery"};
                service_snapshot=1;
                for(unsigned i=0;i<sizeof(services)/sizeof(services[0]);i++){
                    int found=check_service(services[i]);
                    fprintf(stderr,"FRAMEWORK_SERVICE name=%s status=%d\n",services[i],found);
                    drain_logs();drain_property_requests();
                }
            }
        }
        if(compositor>0){
            int sf_status;
            if(waitpid(compositor,&sf_status,WNOHANG)==compositor){
                fprintf(stderr,"GRAPHICS_SF_DIED=%d\n",sf_status);return 99;
            }
        }
        if(virtual_battery>0){
            int battery_status;
            if(waitpid(virtual_battery,&battery_status,WNOHANG)==virtual_battery){fprintf(stderr,"GRAPHICS_BATTERY_DIED=%d\n",battery_status);return 99;}
        }
        if(netd>0){int st;if(waitpid(netd,&st,WNOHANG)==netd){fprintf(stderr,"NETD_DIED=%d\n",st);return 99;}}
        if(vold>0){int st;if(waitpid(vold,&st,WNOHANG)==vold){fprintf(stderr,"VOLD_DIED=%d\n",st);return 99;}}
        if(audioserver>0){int st;if(waitpid(audioserver,&st,WNOHANG)==audioserver){fprintf(stderr,"AUDIO_DIED=%d\n",st);return 99;}}
        if(keystore>0){int st;if(waitpid(keystore,&st,WNOHANG)==keystore){fprintf(stderr,"KEYSTORE_DIED=%d\n",st);return 99;}}
        if(manager>0){int st;if(waitpid(manager,&st,WNOHANG)==manager){fprintf(stderr,"SERVICEMANAGER_DIED=%d\n",st);return 99;}}
        if(textservices_stub>0){int st;if(waitpid(textservices_stub,&st,WNOHANG)==textservices_stub){fprintf(stderr,"TEXTSERVICES_NULL_STUB_DIED=%d\n",st);return 99;}}
        if(w==child)break;
        if(w<0 && errno!=EINTR)return 93;
        if(query>=0 && !ready){
            struct pollfd pf={query,POLLIN,0};
            if(poll(&pf,1,0)>0 && (pf.revents&POLLIN)){
                char reply[128]={0};unsigned length;
                if(read(query,&length,4)==4 && ntohl(length)<sizeof(reply)){
                    unsigned n=ntohl(length);
                    if(read(query,reply,n)==n && !strcmp(reply,"armeabi-v7a,armeabi")){
                        fprintf(stderr,"ZYGOTE_ABI_READY=%s\n",reply);ready=1;
                        if(zygote && !zygote_system_server){
                            report_zygote_open_fds(child);
                            close(query);query=socket(AF_UNIX,SOCK_STREAM,0);
                            struct sockaddr_un fork_socket={.sun_family=AF_UNIX};
                            strcpy(fork_socket.sun_path,"/dev/socket/zygote");
                            if(query<0 || connect(query,(struct sockaddr*)&fork_socket,sizeof(fork_socket)))return 97;
                            /* Android 7 requires --runtime-args before the class.
                             * Do not pass diagnostic FDs: Zygote validates its FD allowlist. */
                            const char request[]="9\n--runtime-args\n--setuid=1000\n--setgid=1000\n--setgroups=1000,1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1018,1021,1032,3001,3002,3003,3006,3007,3009\n--target-sdk-version=25\n--nice-name=kindle_zygote_probe\ncom.android.internal.os.RuntimeInit\nzygote\napplication\n";
                            if(write_exact(query,request,sizeof(request)-1))return 100;
                            clock_gettime(CLOCK_MONOTONIC,&fork_request_time);
                            fork_sent=1;fprintf(stderr,"ZYGOTE_FORK_REQUEST=sent uid=1000 gid=1000\n");
                        }else{if(!zygote_system_server)kill(child,SIGTERM);close(query);query=-1;}
                    }
                }
            }
        }else if(query>=0 && zygote && fork_sent && !fork_response){
            struct pollfd pf={query,POLLIN,0};
            int poll_rc=poll(&pf,1,0);
            if(poll_rc>0 && (pf.revents&(POLLIN|POLLHUP|POLLERR))){
                ssize_t n=recv(query,fork_response_data+fork_response_have,
                               sizeof(fork_response_data)-fork_response_have,MSG_DONTWAIT);
                if(n>0){
                    fork_response_have+=(size_t)n;
                    fprintf(stderr,"ZYGOTE_FORK_RESPONSE_BYTES=%zu\n",fork_response_have);
                    if(fork_response_have==sizeof(fork_response_data)){
                        memcpy(&fork_pid_wire,fork_response_data,4);fork_pid=(pid_t)(int32_t)ntohl(fork_pid_wire);
                        fork_wrapper=fork_response_data[4]!=0;fork_response=1;
                        fprintf(stderr,"ZYGOTE_FORK_RESPONSE pid=%d wrapper=%d\n",fork_pid,fork_wrapper);
                        close(query);query=-1;
                        if(fork_pid<=0)return 102;
                    }
                }else if(n==0){fprintf(stderr,"ZYGOTE_FORK_SOCKET_CLOSED revents=%x bytes=%zu\n",pf.revents,fork_response_have);report_getsockname_trace();return 101;}
                else if(errno!=EAGAIN && errno!=EINTR){fprintf(stderr,"ZYGOTE_FORK_SOCKET_READ_FAILED errno=%d revents=%x\n",errno,pf.revents);return 101;}
            }
            if(!fork_wait_reported){
                struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
                if(now.tv_sec-fork_request_time.tv_sec>=5){
                    fprintf(stderr,"ZYGOTE_FORK_WAIT seconds=%ld bytes=%zu\n",(long)(now.tv_sec-fork_request_time.tv_sec),fork_response_have);
                    report_zygote_server_state(child);
                    if(kill(child,SIGQUIT)==0){
                        fprintf(stderr,"ZYGOTE_SIGQUIT_SENT pid=%d\n",child);
                        report_zygote_java_traces();
                    }
                    else
                        fprintf(stderr,"ZYGOTE_SIGQUIT_FAILED pid=%d errno=%d\n",child,errno);
                    fork_wait_reported=1;
                }
            }
        }
        if(zygote && fork_pid>0 && child_identity==-2){
            int identity=report_zygote_child_identity(fork_pid);
            if(identity!=0)child_identity=identity;
            if(identity<0)return 103;
        }
        if(zygote && fork_response && child_identity==1 && zygote_hello_result && !zygote_shutdown_sent){
            fprintf(stderr,"ZYGOTE_APP_SPAWN_PASSED uid=1000 gid=1000 mountns=child-private\n");
            if(child>0)kill(child,SIGTERM);
            zygote_shutdown_sent=1;
        }
        usleep(graphics?5000:50000);
    }
    if(zygote)report_getsockname_trace();
    if(ready&&zygote&&fork_response&&child_identity==1&&zygote_hello_result){fprintf(stderr,"ZYGOTE_PROBE_CLEAN_EXIT\n");return 0;}
    if(ready&&!zygote){fprintf(stderr,"ZYGOTE_PROBE_CLEAN_EXIT\n");return 0;}
    if(WIFEXITED(status)){fprintf(stderr,"ART_EXIT=%d\n",WEXITSTATUS(status));return WEXITSTATUS(status);}
    fprintf(stderr,"ART_SIGNAL=%d\n",WTERMSIG(status));return 128+WTERMSIG(status);
}
