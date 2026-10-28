#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef int (*getsockname_fn)(int, struct sockaddr *, socklen_t *);
static void trace_event(const char *format, ...) {
    const char *path = getenv("KINDLE_FDTRACE_PATH");
    if (!path || !*path) return;
    char line[512]; va_list ap; va_start(ap, format);
    int n = vsnprintf(line, sizeof(line)-1, format, ap); va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(line)-1) n = sizeof(line)-2;
    line[n++]='\n';
    int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
    if(fd>=0){(void)write(fd,line,(size_t)n);close(fd);}
}
__attribute__((constructor)) static void trace_loaded(void) {
    const char *path = getenv("KINDLE_FDTRACE_PATH");
    if (!path || !*path) path = "/data/local/tmp/getsockname-trace.log";
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) { static const char message[] = "TRACE_LIBRARY_LOADED\n"; (void)write(fd, message, sizeof(message)-1); close(fd); }
}
typedef int (*socket_fn)(int, int, int);
int socket(int domain, int type, int protocol) {
    static socket_fn real_socket;
    if(!real_socket)real_socket=(socket_fn)dlsym(RTLD_NEXT,"socket");
    if(!real_socket){errno=ENOSYS;return -1;}
    int fd=real_socket(domain,type,protocol),saved_errno=errno;
    if(domain==AF_UNIX)trace_event("SOCKET fd=%d type=%d protocol=%d errno=%d",fd,type,protocol,fd<0?saved_errno:0);
    errno=saved_errno;return fd;
}
typedef int (*socketpair_fn)(int, int, int, int[2]);
int socketpair(int domain,int type,int protocol,int pair[2]){
    static socketpair_fn real_socketpair;
    if(!real_socketpair)real_socketpair=(socketpair_fn)dlsym(RTLD_NEXT,"socketpair");
    if(!real_socketpair){errno=ENOSYS;return -1;}
    int rc=real_socketpair(domain,type,protocol,pair),saved_errno=errno;
    if(domain==AF_UNIX)trace_event("SOCKETPAIR rc=%d fds=%d,%d type=%d errno=%d",rc,rc==0?pair[0]:-1,rc==0?pair[1]:-1,type,rc<0?saved_errno:0);
    errno=saved_errno;return rc;
}
typedef int (*accept4_fn)(int,struct sockaddr *,socklen_t *,int);
typedef int (*accept_fn)(int,struct sockaddr *,socklen_t *);
int accept(int fd,struct sockaddr *address,socklen_t *length){
    static accept_fn real_accept;
    if(!real_accept)real_accept=(accept_fn)dlsym(RTLD_NEXT,"accept");
    if(!real_accept){errno=ENOSYS;return -1;}
    int accepted=real_accept(fd,address,length),saved_errno=errno;
    if(accepted>=0 && address && address->sa_family==AF_UNIX)trace_event("ACCEPT listen=%d accepted=%d len=%u",fd,accepted,length?(unsigned)*length:0);
    errno=saved_errno;return accepted;
}
int accept4(int fd,struct sockaddr *address,socklen_t *length,int flags){
    static accept4_fn real_accept4;
    if(!real_accept4)real_accept4=(accept4_fn)dlsym(RTLD_NEXT,"accept4");
    if(!real_accept4){errno=ENOSYS;return -1;}
    int accepted=real_accept4(fd,address,length,flags),saved_errno=errno;
    if(accepted>=0 && address && address->sa_family==AF_UNIX)trace_event("ACCEPT4 listen=%d accepted=%d len=%u flags=%d",fd,accepted,length?(unsigned)*length:0,flags);
    errno=saved_errno;return accepted;
}
int getsockname(int fd, struct sockaddr *address, socklen_t *length) {
    static getsockname_fn real_getsockname;
    if (!real_getsockname) real_getsockname = (getsockname_fn)dlsym(RTLD_NEXT, "getsockname");
    if (!real_getsockname) { errno = ENOSYS; return -1; }
    int rc = real_getsockname(fd, address, length);
    int saved_errno = errno;
    if (address && length && address->sa_family == AF_UNIX) {
        const char *path = getenv("KINDLE_FDTRACE_PATH");
        if (path && *path) {
            char line[512];
            struct stat st; int have_stat=fstat(fd,&st)==0;
            int accepting=-1; socklen_t option_len=sizeof(accepting);
            if(getsockopt(fd,SOL_SOCKET,SO_ACCEPTCONN,&accepting,&option_len))accepting=-1;
            struct sockaddr_storage peer; socklen_t peer_len=sizeof(peer);
            int peer_rc=getpeername(fd,(struct sockaddr*)&peer,&peer_len);
            int used = snprintf(line, sizeof(line), "fd=%d rc=%d errno=%d family=%d len=%u inode=%llu acceptconn=%d peer_rc=%d peer_family=%d peer_len=%u path=", fd, rc,
                                rc ? saved_errno : 0, address->sa_family, (unsigned)*length,
                                (unsigned long long)(have_stat?st.st_ino:0), accepting, peer_rc,
                                peer_rc==0?peer.ss_family:-1, peer_rc==0?(unsigned)peer_len:0);
            if (rc == 0 && *length > offsetof(struct sockaddr_un, sun_path)) {
                const struct sockaddr_un *un = (const struct sockaddr_un *)address;
                size_t n = *length - offsetof(struct sockaddr_un, sun_path);
                if (n > sizeof(un->sun_path)) n = sizeof(un->sun_path);
                for (size_t i = 0; i < n && used < (int)sizeof(line) - 5; ++i) {
                    unsigned char c = (unsigned char)un->sun_path[i];
                    if (c == 0) { line[used++]='\\'; line[used++]='0'; }
                    else if (c >= 32 && c < 127) line[used++]=(char)c;
                    else used += snprintf(line + used, sizeof(line) - (size_t)used, "\\x%02x", c);
                }
            }
            if(peer_rc==0 && peer.ss_family==AF_UNIX && peer_len>offsetof(struct sockaddr_un,sun_path) && used<(int)sizeof(line)-16){
                const struct sockaddr_un *un=(const struct sockaddr_un*)&peer;
                size_t n=peer_len - offsetof(struct sockaddr_un,sun_path);
                if(n>sizeof(un->sun_path))n=sizeof(un->sun_path);
                used+=snprintf(line+used,sizeof(line)-(size_t)used," peer_path=");
                for(size_t i=0;i<n && used<(int)sizeof(line)-5;i++){
                    unsigned char c=(unsigned char)un->sun_path[i];
                    if(c==0){line[used++]='\\';line[used++]='0';}
                    else if(c>=32 && c<127)line[used++]=(char)c;
                    else used+=snprintf(line+used,sizeof(line)-(size_t)used,"\\x%02x",c);
                }
            }
            if (used < (int)sizeof(line) - 1) line[used++]='\n';
            int out = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
            if (out >= 0) { (void)write(out, line, (size_t)used); close(out); }
        }
    }
    errno = saved_errno;
    return rc;
}
typedef void (*android_log_close_fn)(void);
void __android_log_close(void) {
    static android_log_close_fn real_close;
    if(!real_close)real_close=(android_log_close_fn)dlsym(RTLD_NEXT,"__android_log_close");
    if(real_close)real_close();
    DIR *directory=opendir("/proc/self/fd");
    if(!directory)return;
    int directory_fd=dirfd(directory);struct dirent *entry;
    while((entry=readdir(directory))!=NULL){
        const char *name=entry->d_name;int numeric=*name!=0;
        for(const char *c=name;*c;c++)if(*c<'0'||*c>'9')numeric=0;
        if(!numeric)continue;
        int fd=atoi(name);if(fd<=2 || fd==directory_fd)continue;
        struct sockaddr_storage peer;socklen_t length=sizeof(peer);
        if(getpeername(fd,(struct sockaddr*)&peer,&length) || peer.ss_family!=AF_UNIX ||
           length<=offsetof(struct sockaddr_un,sun_path))continue;
        const struct sockaddr_un *un=(const struct sockaddr_un*)&peer;
        size_t path_len=length - offsetof(struct sockaddr_un,sun_path);
        static const char log_path[]="/dev/socket/logdw";
        if(path_len==sizeof(log_path) && memcmp(un->sun_path,log_path,sizeof(log_path))==0){
            if(close(fd)==0)trace_event("LOGFD_CLOSED fd=%d peer=/dev/socket/logdw",fd);
        }
    }
    closedir(directory);
}
