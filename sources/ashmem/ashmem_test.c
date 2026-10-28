/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <cutils/ashmem.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d errno=%d\n",__LINE__,errno);return 1;}}while(0)
int main(void){
    alarm(6);setbuf(stdout,NULL);
    int fd=ashmem_create_region("semantic-test",4096);
    if(fd<0){fprintf(stderr,"ASHMEM_CREATE_FAILED errno=%d\n",errno);return 3;}
    CHECK(ashmem_get_size_region(fd)==4096);
    CHECK(ftruncate(fd,8192)<0 && errno==EPERM);
    unsigned *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    CHECK(p!=MAP_FAILED);*p=2026;
    int pair[2];CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    pid_t child=fork();CHECK(child>=0);
    if(child==0){
        close(fd);close(pair[0]);
        char control[CMSG_SPACE(sizeof(int))],c;
        struct iovec v={&c,1};struct msghdr m={0};m.msg_iov=&v;m.msg_iovlen=1;
        m.msg_control=control;m.msg_controllen=sizeof(control);
        CHECK(recvmsg(pair[1],&m,0)==1);
        struct cmsghdr *h=CMSG_FIRSTHDR(&m);CHECK(h && h->cmsg_type==SCM_RIGHTS);
        int received;memcpy(&received,CMSG_DATA(h),sizeof(received));
        CHECK(ashmem_get_size_region(received)==4096);
        unsigned *q=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,received,0);
        CHECK(q!=MAP_FAILED && *q==2026);*q=2027;_exit(0);
    }
    close(pair[1]);char control[CMSG_SPACE(sizeof(int))]={0},c='x';
    struct iovec v={&c,1};struct msghdr m={0};m.msg_iov=&v;m.msg_iovlen=1;
    m.msg_control=control;m.msg_controllen=sizeof(control);
    struct cmsghdr *h=CMSG_FIRSTHDR(&m);h->cmsg_level=SOL_SOCKET;h->cmsg_type=SCM_RIGHTS;
    h->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(h),&fd,sizeof(fd));
    CHECK(sendmsg(pair[0],&m,0)==1);int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(status==0 && *p==2027);
    CHECK(ashmem_unpin_region(fd,0,0)==0 && ashmem_pin_region(fd,0,0)==ASHMEM_NOT_PURGED);
    CHECK(ashmem_pin_region(fd,1,4096)<0 && errno==EINVAL);
    CHECK(ashmem_set_prot_region(fd,PROT_READ)<0 && errno==EOPNOTSUPP);
    CHECK(ashmem_set_prot_region(fd,PROT_READ|PROT_EXEC)==0);
    *p=2030;CHECK(*p==2030); // Existing writable mappings remain usable.
    CHECK(mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)==MAP_FAILED);
    void *rx=mmap(NULL,4096,PROT_READ|PROT_EXEC,MAP_SHARED,fd,0);CHECK(rx!=MAP_FAILED);
    CHECK(ashmem_set_prot_region(fd,PROT_READ|PROT_WRITE|PROT_EXEC)<0 && errno==EPERM);
    int regular=open("/system/build.prop",O_RDONLY);CHECK(regular>=0);
    CHECK(ashmem_get_size_region(regular)<0 && errno==EINVAL);
    close(regular);close(fd);munmap(p,4096);munmap(rx,4096);
    puts("ASHMEM_SUBSET_PASS: size seals SCM_RIGHTS shared-data ranges protections unsupported-rejected");
    return 0;
}
