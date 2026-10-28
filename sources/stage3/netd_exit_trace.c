/* Probe-only interposer to identify the netd exit(1) caller. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>

typedef int (*setsockopt_fn)(int, int, int, const void *, socklen_t);

/* The Kindle kernel rejects netd's privileged receive-buffer request even
 * inside this probe's private network namespace. Retry that one option with
 * the ordinary, kernel-capped SO_RCVBUF setting. */
__attribute__((visibility("default")))
int setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    static setsockopt_fn real_setsockopt;
    if (!real_setsockopt)
        real_setsockopt = (setsockopt_fn)dlsym(RTLD_NEXT, "setsockopt");
    if (!real_setsockopt) { errno = ENOSYS; return -1; }
    int rc = real_setsockopt(fd, level, option, value, length);
    int original_errno = errno;
    if (rc == -1 && level == SOL_SOCKET && option == SO_RCVBUFFORCE) {
        int fallback = real_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, value, length);
        int fallback_errno = errno;
        char message[192];
        int n = snprintf(message, sizeof(message),
                "NETD_RCVBUFFORCE_FALLBACK original_errno=%d fallback_rc=%d fallback_errno=%d\n",
                original_errno, fallback, fallback ? fallback_errno : 0);
        if (n > 0) (void)write(STDERR_FILENO, message,
                (size_t)n < sizeof(message) ? (size_t)n : sizeof(message));
        errno = fallback ? fallback_errno : original_errno;
        return fallback;
    }
    errno = original_errno;
    return rc;
}

__attribute__((visibility("default"), noinline, noreturn))
void exit(int status) {
    int saved_errno = errno;
    void *caller = __builtin_return_address(0);
    Dl_info info = {0};
    int found = dladdr(caller, &info);
    char message[768];
    int length = snprintf(message, sizeof(message),
            "NETD_EXIT_TRACE status=%d errno=%d pc=%p image=%s offset=%p symbol=%s\n",
            status, saved_errno, caller,
            found && info.dli_fname ? info.dli_fname : "?",
            found && info.dli_fbase ? (void *)((uintptr_t)caller-(uintptr_t)info.dli_fbase) : NULL,
            found && info.dli_sname ? info.dli_sname : "?");
    if (length > 0) {
        if ((size_t)length > sizeof(message)) length = sizeof(message);
        (void)write(STDERR_FILENO, message, (size_t)length);
    }
    syscall(SYS_exit_group, status);
    __builtin_unreachable();
}

/* Android 7 private FRA_UID_START/END (18/19) became Linux FRA_PAD/L3MDEV.
 * Translate to the upstream FRA_UID_RANGE (20), preserving real kernel ACKs. */
#include <sys/uio.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/fib_rules.h>
#include <stdlib.h>
#include <string.h>
__attribute__((visibility("default")))
ssize_t writev(int fd,const struct iovec *iov,int count) {
    typedef ssize_t (*fn)(int,const struct iovec*,int);
    static fn original;
    if(!original)original=(fn)dlsym(RTLD_NEXT,"writev");
    if(!original){errno=ENOSYS;return -1;}
    size_t size=0;for(int i=0;i<count;i++){if(iov[i].iov_len>65536-size)return original(fd,iov,count);size+=iov[i].iov_len;}
    if(size<NLMSG_LENGTH(sizeof(struct fib_rule_hdr)))return original(fd,iov,count);
    unsigned char *input=malloc(size),*output=malloc(size+16);
    if(!input||!output){free(input);free(output);return original(fd,iov,count);}
    size_t pos=0;for(int i=0;i<count;i++){memcpy(input+pos,iov[i].iov_base,iov[i].iov_len);pos+=iov[i].iov_len;}
    struct nlmsghdr *header=(struct nlmsghdr*)input;
    if((header->nlmsg_type!=RTM_NEWRULE&&header->nlmsg_type!=RTM_DELRULE)||header->nlmsg_len!=size){free(input);free(output);return original(fd,iov,count);}
    size_t offset=NLMSG_LENGTH(sizeof(struct fib_rule_hdr)),used=offset;
    memcpy(output,input,offset);uint32_t start=0,end=0;int seen=0,valid=1;
    while(offset<size){
        if(size-offset<sizeof(struct rtattr)){valid=0;break;}
        struct rtattr *a=(struct rtattr*)(input+offset);size_t n=RTA_ALIGN(a->rta_len);
        if(a->rta_len<sizeof(*a)||n>size-offset){valid=0;break;}
        if((a->rta_type==18||a->rta_type==19)&&a->rta_len==RTA_LENGTH(4)){
            if(a->rta_type==18){memcpy(&start,RTA_DATA(a),4);seen|=1;}else{memcpy(&end,RTA_DATA(a),4);seen|=2;}
        }else{memcpy(output+used,a,n);used+=n;}
        offset+=n;
    }
    if(!valid||seen!=3){free(input);free(output);return original(fd,iov,count);}
    struct rtattr *range=(struct rtattr*)(output+used);range->rta_type=20;range->rta_len=RTA_LENGTH(8);
    memcpy(RTA_DATA(range),&start,4);memcpy((char*)RTA_DATA(range)+4,&end,4);used+=RTA_ALIGN(range->rta_len);
    ((struct nlmsghdr*)output)->nlmsg_len=used;
    struct iovec one={output,used};ssize_t result=original(fd,&one,1);int saved=errno;
    if(result>=0){static int logs;if(logs++<12)fprintf(stderr,"NETD_UID_RULE_UPSTREAM_ABI=1 start=%u end=%u family=%u\n",start,end,((struct fib_rule_hdr*)NLMSG_DATA(header))->family);result=size;}
    free(input);free(output);errno=saved;return result;
}
