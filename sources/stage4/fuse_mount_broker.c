/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "fuse_mount_protocol.h"

static int allowed_destination(const char *path) {
    return !strcmp(path, "/mnt/runtime/default/emulated") ||
           !strcmp(path, "/mnt/runtime/read/emulated") ||
           !strcmp(path, "/mnt/runtime/write/emulated");
}

static int set_broker_capability(void) {
    struct __user_cap_header_struct header = {
        _LINUX_CAPABILITY_VERSION_3, 0
    };
    struct __user_cap_data_struct caps[2];
    memset(caps, 0, sizeof(caps));
    const unsigned word = CAP_SYS_ADMIN / 32;
    const unsigned bit = 1U << (CAP_SYS_ADMIN % 32);
    caps[word].effective = caps[word].permitted = bit;
    if (syscall(__NR_capset, &header, caps) != 0) return -1;
    if (prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        errno = EPERM;
        return -1;
    }
    struct __user_cap_data_struct verify[2];
    memset(verify, 0, sizeof(verify));
    if (syscall(__NR_capget, &header, verify) != 0 ||
        verify[word].effective != bit || verify[word].permitted != bit ||
        verify[word].inheritable != 0 ||
        verify[1].effective != 0 || verify[1].permitted != 0 || verify[1].inheritable != 0) {
        errno = EPERM;
        return -1;
    }
    FILE *map = fopen("/proc/self/uid_map", "r");
    unsigned long long inside = 0, outside = 0, length = 0;
    int map_ok = map && fscanf(map, "%llu %llu %llu", &inside, &outside, &length) == 3 &&
        inside == 0 && outside == 0 && length == 65536;
    if (map) fclose(map);
    map = fopen("/proc/self/gid_map", "r");
    inside = outside = length = 0;
    map_ok = map_ok && map && fscanf(map, "%llu %llu %llu", &inside, &outside, &length) == 3 &&
        inside == 0 && outside == 0 && length == 65536;
    if (map) fclose(map);
    FILE *status = fopen("/proc/self/status", "r");
    char line[256];
    int no_new_privs = 0, seccomp = 0;
    if (status) {
        while (fgets(line, sizeof(line), status)) {
            if (sscanf(line, "NoNewPrivs:%d", &no_new_privs) == 1) continue;
            if (sscanf(line, "Seccomp:%d", &seccomp) == 1) continue;
        }
        fclose(status);
    }
    if (!map_ok || no_new_privs != 1 || seccomp != 2) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int receive_request(int client, struct kindlelab_fuse_request *request,
                           int *fuse_fd) {
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {request, sizeof(*request)};
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t n;
    do { n = recvmsg(client, &message, 0); } while (n < 0 && errno == EINTR);
    if (n != (ssize_t)sizeof(*request) || (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC))) {
        errno = EPROTO;
        return -1;
    }
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        errno = EPROTO;
        return -1;
    }
    memcpy(fuse_fd, CMSG_DATA(cmsg), sizeof(*fuse_fd));
    return 0;
}

static void serve_client(int client) {
    struct ucred peer;
    memset(&peer, 0, sizeof(peer));
    socklen_t peer_size = sizeof(peer);
    struct kindlelab_fuse_request request;
    memset(&request, 0, sizeof(request));
    int fuse_fd = -1;
    int result = -1;
    int error = EPERM;
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0) {
        error = errno;
        goto reply;
    }
    if (receive_request(client, &request, &fuse_fd) != 0) {
        /* Empty connections are used by the parent to check readiness. */
        if (errno == EPROTO) goto reply;
        error = errno;
        goto reply;
    }
    if (peer.uid != 0 || peer.gid != 0 ||
        request.magic != KINDLELAB_FUSE_MAGIC || request.uid != 1023 || request.gid != 1023 ||
        !memchr(request.destination, '\0', sizeof(request.destination)) ||
        !allowed_destination(request.destination)) {
        error = EPERM;
        goto reply;
    }

    if (umount2(request.destination, MNT_DETACH) != 0 &&
        errno != EINVAL && errno != ENOENT) {
        error = errno;
        goto reply;
    }
    char options[192];
    int length = snprintf(options, sizeof(options),
            "fd=%d,rootmode=40000,default_permissions,allow_other,user_id=1023,group_id=1023",
            fuse_fd);
    if (length < 0 || (size_t)length >= sizeof(options)) {
        error = EOVERFLOW;
        goto reply;
    }
    if (mount("/dev/fuse", request.destination, "fuse",
              MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME, options) != 0) {
        error = errno;
        goto reply;
    }
    result = 0;
    error = 0;

reply:
    if (fuse_fd >= 0) close(fuse_fd);
    do { } while (send(client, &error, sizeof(error), MSG_NOSIGNAL) < 0 && errno == EINTR);
    if (result == 0)
        fprintf(stderr, "FUSE_BROKER_MOUNT_OK destination=%s peer=%u:%u\n",
                request.destination, (unsigned)peer.uid, (unsigned)peer.gid);
    else
        fprintf(stderr, "FUSE_BROKER_MOUNT_FAIL errno=%d destination=%s\n",
                error, request.destination);
}

int main(void) {
    if (getuid() != 0 || geteuid() != 0 || set_broker_capability() != 0) {
        fprintf(stderr, "FUSE_BROKER_CAPABILITY_INVALID errno=%d\n", errno);
        return 90;
    }
    umask(077);
    int server = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (server < 0) { perror("FUSE_BROKER_SOCKET"); return 91; }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, KINDLELAB_FUSE_SOCKET, sizeof(KINDLELAB_FUSE_SOCKET));
    unlink(KINDLELAB_FUSE_SOCKET);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(KINDLELAB_FUSE_SOCKET, 0600) != 0 || listen(server, 4) != 0) {
        perror("FUSE_BROKER_LISTEN"); close(server); unlink(KINDLELAB_FUSE_SOCKET); return 92;
    }
    fprintf(stderr, "FUSE_BROKER_READY path=%s cap_sys_admin=private-userns only=three-emulated-mountpoints\n",
            KINDLELAB_FUSE_SOCKET);
    for (;;) {
        int client;
        do { client = accept(server, NULL, NULL); } while (client < 0 && errno == EINTR);
        if (client < 0) { perror("FUSE_BROKER_ACCEPT"); continue; }
        serve_client(client);
        close(client);
    }
}
