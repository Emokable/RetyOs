/* SPDX-License-Identifier: Apache-2.0
 * Laboratory libcutils ashmem API subset, backed by sealed memfd.
 * No purging. No device/ioctl emulation. Unsupported protections fail closed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cutils/ashmem.h>
#ifdef ASHMEM_POOL_ENABLED
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/statvfs.h>
static int is_noexec(int fd){struct statvfs fs;return !fstatvfs(fd,&fs) && (fs.f_flag&ST_NOEXEC);}
static int borrow_pool(size_t size){
    int sock=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(sock<0)return -1;
    struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,"/dev/socket/ashmem_pool");
    struct timeval timeout={2,0};setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    uint32_t requested=(uint32_t)size;
    if(connect(sock,(struct sockaddr*)&address,sizeof(address)) || send(sock,&requested,sizeof(requested),MSG_NOSIGNAL)!=sizeof(requested)){int e=errno;close(sock);errno=e;return -1;}
    char byte,control[CMSG_SPACE(sizeof(int))];struct iovec iov={&byte,1};struct msghdr msg={0};
    msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control;msg.msg_controllen=sizeof(control);
    int fd=-1;
    if(recvmsg(sock,&msg,MSG_CMSG_CLOEXEC)==1){
        struct cmsghdr *c=CMSG_FIRSTHDR(&msg);
        if(c && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len==CMSG_LEN(sizeof(int)))memcpy(&fd,CMSG_DATA(c),sizeof(fd));
    }
    close(sock);
    if(fd>=0 && !is_noexec(fd)){close(fd);fd=-1;}
    if(fd<0)errno=ENOSPC;
    return fd;
}
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#endif
#define SEAL_SHRINK 2
#define SEAL_GROW 4
#define SEAL_FUTURE_WRITE 16
static const char prefix[]="/memfd:kindle-a7-ashmem:";
static int validate(int fd,struct stat *st){
    char path[48],name[256];snprintf(path,sizeof(path),"/proc/self/fd/%d",fd);
    ssize_t n=readlink(path,name,sizeof(name)-1);
    if(n<0)return -1;name[n]=0;
    if(strncmp(name,prefix,sizeof(prefix)-1)){
#ifdef ASHMEM_POOL_ENABLED
        if(strncmp(name,"/dev/ashmem-pool/slot",21) || !is_noexec(fd))
#endif
        {errno=EINVAL;return -1;}
    }
    int seals=fcntl(fd,F_GET_SEALS);
    if(seals<0)return -1;
    if((seals&(SEAL_SHRINK|SEAL_GROW))!=(SEAL_SHRINK|SEAL_GROW)){
        errno=EINVAL;return -1;
    }
    if(fstat(fd,st))return -1;
    if(!S_ISREG(st->st_mode)){errno=EINVAL;return -1;}
    return 0;
}
int ashmem_create_region(const char *name,size_t size){
    if(size>INT_MAX){errno=EOVERFLOW;return -1;}
#ifdef ASHMEM_POOL_ENABLED
    if(size==2U*1024U*1024U){
        int fd=borrow_pool(size);
        fprintf(stderr,"ASHMEM_NOEXEC_POOL name=%s size=%u fd=%d errno=%d\n",name?name:"",(unsigned)size,fd,fd<0?errno:0);
        return fd;
    }
#endif
    char label[240];snprintf(label,sizeof(label),"kindle-a7-ashmem:%.200s",name?name:"");
    int fd=(int)syscall(385 /* ARM EABI memfd_create */,label,3);
    if(fd<0)return -1;
    if(ftruncate(fd,(off_t)size) || fcntl(fd,F_ADD_SEALS,SEAL_SHRINK|SEAL_GROW)){
        int e=errno;close(fd);errno=e;return -1;
    }
    return fd;
}
int ashmem_get_size_region(int fd){
    struct stat st;if(validate(fd,&st))return -1;
    if(st.st_size>INT_MAX){errno=EOVERFLOW;return -1;}
    return (int)st.st_size;
}
int ashmem_set_prot_region(int fd,int prot){
    struct stat st;if(validate(fd,&st))return -1;
#ifdef ASHMEM_FRAMEWORK_RWX_MAX
    if(prot==PROT_READ || prot==(PROT_READ|PROT_WRITE)){
        static int reported=0;
        if(!reported){fprintf(stderr,"ASHMEM_FRAMEWORK_LAB maximum_has_exec=1 exec_removal_enforced=0\n");reported=1;}
        prot|=PROT_EXEC;
    }
#endif
    int seals=fcntl(fd,F_GET_SEALS);
#ifdef ASHMEM_POOL_ENABLED
    if(is_noexec(fd)){
        if(prot==(PROT_READ|PROT_WRITE)){if(seals&SEAL_FUTURE_WRITE){errno=EPERM;return -1;}return 0;}
        if(prot==PROT_READ)return fcntl(fd,F_ADD_SEALS,SEAL_FUTURE_WRITE);
        errno=EOPNOTSUPP;return -1;
    }
#endif
    if(prot==(PROT_READ|PROT_WRITE|PROT_EXEC)){
        if(seals&SEAL_FUTURE_WRITE){errno=EPERM;return -1;}
        return 0;
    }
    if(prot==(PROT_READ|PROT_EXEC))return fcntl(fd,F_ADD_SEALS,SEAL_FUTURE_WRITE);
    // Linux 5.15 cannot enforce removal of read/execute access on memfd.
    errno=EOPNOTSUPP;return -1;
}
static int range_ok(int fd,size_t offset,size_t len){
    struct stat st;if(validate(fd,&st))return -1;
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    size_t rounded=((size_t)st.st_size+page-1)&~(page-1);
    if(offset%page || (len && len%page) || offset>rounded ||
       (len && len>rounded-offset)){errno=EINVAL;return -1;}
    return 0;
}
int ashmem_pin_region(int fd,size_t offset,size_t len){
    return range_ok(fd,offset,len)?-1:ASHMEM_NOT_PURGED;
}
int ashmem_unpin_region(int fd,size_t offset,size_t len){
    return range_ok(fd,offset,len)?-1:0;
}
