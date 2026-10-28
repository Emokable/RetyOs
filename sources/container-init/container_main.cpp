/* SPDX-License-Identifier: Apache-2.0
 * Container entry point for the AOSP 7.1 init parser/service engine.
 * This deliberately excludes Android's hardware/bootstrap builtins.
 */
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <stddef.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include "action.h"
#include "builtins.h"
#include "init.h"
#include "init_parser.h"
#include "property_service.h"
#include "service.h"
#include "util.h"

const char *ENV[32] = {"PATH=/system/bin", "ANDROID_ROOT=/system", nullptr};
bool waiting_for_exec=false;
int have_console=0;
std::string console_name;
struct selabel_handle *sehandle=nullptr,*sehandle_prop=nullptr;
static volatile sig_atomic_t stopping=0;
static int parse_errors=0,child_errors=0,client_ok=0,absent_ok=0;
static void expired(int){_exit(124);}
static void stop_signal(int){stopping=1;}
void lab_parse_error(){++parse_errors;}
void lab_child_exit(const char *name,int status){
    if(!strcmp(name,"lab_client") || !strcmp(name,"lab_absent")){
        if(status!=0)++child_errors;
        else if(!strcmp(name,"lab_client"))client_ok=1;else absent_ok=1;
    }
}
void init_klog_write(int,const char *fmt,...){
    va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);
}
int selinux_klog_callback(int,const char*,...){return 0;}
int selinux_reload_policy(){errno=EPERM;return -1;}
extern "C" int android_reboot(int,int,const char*){errno=EPERM;return -1;}
int add_environment(const char *key,const char *value){
    std::string item=std::string(key)+"="+value;
    for(unsigned i=0;i<31;i++){
        if(!ENV[i] || (!strncmp(ENV[i],key,strlen(key)) && ENV[i][strlen(key)]=='=')){
            ENV[i]=strdup(item.c_str());return ENV[i]?0:-1;
        }
    }
    return -1;
}
void property_changed(const char *name,const char *value){
    ActionManager::GetInstance().QueuePropertyTrigger(name,value);
}
std::string property_get(const char *name){
    char value[PROP_VALUE_MAX];__system_property_get(name,value);return value;
}
int property_set(const char *name,const char *value){
    if(strlen(name)>=PROP_NAME_MAX || strlen(value)>=PROP_VALUE_MAX)return -1;
    const prop_info *pi=__system_property_find(name);
    if(pi && !strncmp(name,"ro.",3))return -1;
    int rc=pi?__system_property_update(const_cast<prop_info*>(pi),value,strlen(value)):
        __system_property_add(name,strlen(name),value,strlen(value));
    if(!rc){fprintf(stderr,"PROPERTY %s=%s\n",name,value);property_changed(name,value);}
    return rc;
}
static int do_start(const std::vector<std::string>& a){
    Service *s=ServiceManager::GetInstance().FindServiceByName(a[1]);
    return s && s->Start()?0:-1;
}
static int do_stop(const std::vector<std::string>& a){
    Service *s=ServiceManager::GetInstance().FindServiceByName(a[1]);
    if(!s)return -1;s->Stop();return 0;
}
static int do_setprop(const std::vector<std::string>& a){return property_set(a[1].c_str(),a[2].c_str());}
static int do_trigger(const std::vector<std::string>& a){ActionManager::GetInstance().QueueEventTrigger(a[1]);return 0;}
BuiltinFunctionMap::Map& BuiltinFunctionMap::map() const{
    static const Map functions={
        {"start",{1,1,do_start}}, {"stop",{1,1,do_stop}},
        {"setprop",{2,2,do_setprop}}, {"trigger",{1,1,do_trigger}}
    };
    return functions;
}
static bool sandbox(){
    if(prctl(PR_SET_SECUREBITS,SECBIT_NOROOT|SECBIT_NOROOT_LOCKED|
        SECBIT_NO_SETUID_FIXUP|SECBIT_NO_SETUID_FIXUP_LOCKED,0,0,0))return false;
    __user_cap_header_struct header={_LINUX_CAPABILITY_VERSION_3,0};
    __user_cap_data_struct caps[2]={};
    if(syscall(__NR_capset,&header,caps))return false;
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return false;
    std::vector<sock_filter> code;
    code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch)));
    code.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_ARM,1,0));
    code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL));
    code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)));
    const unsigned forbidden[]={__NR_mount,__NR_umount2,__NR_reboot,__NR_swapon,
        __NR_swapoff,__NR_init_module,__NR_delete_module,__NR_ptrace,__NR_syslog,
        __NR_kexec_load,__NR_unshare,__NR_setns,__NR_finit_module,386 /* ARM EABI bpf, newer than NDK kernel headers */,
        __NR_perf_event_open,__NR_open_by_handle_at};
    for(unsigned nr:forbidden){
        code.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,nr,0,1));
        code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM));
    }
    code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW));
    sock_fprog prog={static_cast<unsigned short>(code.size()),code.data()};
    return prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&prog)==0;
}
int main(int,char**){
    setbuf(stderr,nullptr);
    char marker[64]={};FILE *f=fopen("/container.marker","r");
    if(f){fgets(marker,sizeof(marker),f);fclose(f);}
    if(getpid()!=1 || strcmp(marker,"kindle-a7-container\n"))return 90;
    signal(SIGALRM,expired);signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);alarm(12);
    if(!sandbox()){perror("container sandbox");return 91;}
    fprintf(stderr,"AOSP_INIT_CONTAINER pid=%d seccomp=%d no_new_privs=%d\n",
        getpid(),prctl(PR_GET_SECCOMP),prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0));
    int area=__system_property_area_init();
    // -2 means only SELinux xattr setup failed; this laboratory has no policy.
    if(area!=0 && area!=-2){fprintf(stderr,"property_area_init=%d\n",area);return 92;}
    fprintf(stderr,"PROPERTY_AREA result=%d (no host policy loaded)\n",area);
    const BuiltinFunctionMap functions;Action::set_function_map(&functions);
    Parser& parser=Parser::GetInstance();
    parser.AddSectionParser("service",std::make_unique<ServiceParser>());
    parser.AddSectionParser("on",std::make_unique<ActionParser>());
    if(!parser.ParseConfig("/init.container.rc") || parse_errors)return 93;
    auto& am=ActionManager::GetInstance();am.QueueEventTrigger("boot");
    auto& sm=ServiceManager::GetInstance();
    while(!stopping && property_get("sys.kindletest.finished")!="1"){
        if(!waiting_for_exec)am.ExecuteOneCommand();
        sm.ReapAnyOutstandingChildren();
        time_t next=0;sm.ForEachService([&next](Service *s){if(s->flags() & SVC_RESTARTING)s->RestartIfNeeded(next);});
        usleep(10000);
    }
    sm.ForEachService([](Service *s){s->Stop();});
    for(int i=0;i<30;i++){sm.ReapAnyOutstandingChildren();usleep(10000);}
    if(stopping || child_errors || !client_ok || !absent_ok || parse_errors)return 94;
    fprintf(stderr,"AOSP_INIT_CONTAINER_PASS: rc property-trigger start reap stop binder lifecycle\n");
    return 0;
}
