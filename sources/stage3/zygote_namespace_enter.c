#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

#define ANDROID_ID_MAP_SIZE 65536

static int ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) == 0 || errno == EEXIST) return 0;
    fprintf(stderr, "ZYGOTE_USERNS_MKDIR_FAILED path=%s errno=%d (%s)\n",
            path, errno, strerror(errno));
    return -1;
}

static int setup_system_server_mounts(void) {
    /* Read-only sysfs from the container-owned netns exposes tap0's ifindex
     * to netd without exposing writable host device controls. */
    if (ensure_dir("/sys", 0555) != 0 ||
        mount("sysfs", "/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
        goto mount_failed;
    fprintf(stderr, "ANDROID_SYSFS_READONLY=1 network_namespace=owned\n");
    const char *persistent_data = getenv("KINDLE_ANDROID_DATA_PERSISTENT");
    if (!persistent_data || strcmp(persistent_data, "1") != 0) {
        if (mount("tmpfs", "/data", "tmpfs", MS_NODEV | MS_NOSUID,
                  "size=640m,mode=0755") != 0) goto mount_failed;
    } else {
        struct statfs data_fs;
        if (statfs("/data", &data_fs) != 0 ||
            (unsigned long)data_fs.f_type != 0xEF53UL) {
            fprintf(stderr, "ZYGOTE_USERNS_PERSISTENT_DATA_NOT_EXT4 errno=%d (%s)\n",
                    errno, strerror(errno));
            return -1;
        }
        fprintf(stderr, "ZYGOTE_USERNS_PERSISTENT_DATA_IMAGE=1\n");
    }
    if (mount("tmpfs", "/mnt", "tmpfs", MS_NODEV | MS_NOSUID,
              "size=4m,mode=0755") != 0) goto mount_failed;
    if (mount("tmpfs", "/storage", "tmpfs", MS_NODEV | MS_NOSUID,
              "size=4m,mode=0755") != 0) goto mount_failed;

    const char *directories[] = {
        "/data/dalvik-cache", "/data/dalvik-cache/arm", "/data/local",
        "/data/local/tmp", "/data/anr", "/data/system", "/data/system_ce",
        "/data/system_ce/0", "/data/system_de", "/data/system_de/0",
        "/data/app", "/data/app-private", "/data/misc", "/data/misc/user",
        "/data/misc/user/0", "/data/misc/zoneinfo",
        "/data/misc/zoneinfo/current", "/data/system/users",
        "/data/system/heapdump", "/data/system/users/0", "/data/media", "/data/media/0",
        "/data/data", "/data/data/com.android.providers.settings",
        "/data/data/com.android.providers.settings/databases", "/data/user",
        "/data/user_de", "/data/user_de/0", "/data/user_de/0/com.android.providers.settings",
        "/data/user_de/0/com.android.providers.settings/databases",
        "/data/misc/keystore",
        "/mnt/asec", "/mnt/expand", "/mnt/media_rw", "/mnt/user",
        "/mnt/user/0", "/mnt/runtime", "/mnt/runtime/default",
        "/mnt/runtime/default/self", "/mnt/runtime/default/emulated",
        "/mnt/runtime/default/emulated/0", "/mnt/runtime/read",
        "/mnt/runtime/read/self", "/mnt/runtime/write",
        "/mnt/runtime/write/self", "/mnt/runtime/full",
        "/mnt/runtime/full/self", "/storage/self", "/storage/emulated",
        "/storage/emulated/0"
    };
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
        if (ensure_dir(directories[i], 0755) != 0) return -1;

    /* Zygote clones its mount namespace before vold creates emulated volumes.
     * Share only this container-owned subtree so later FUSE mounts reach apps.
     * The namespace root remains private: no propagation to the Kindle host.
     */
    if (mount("/mnt/runtime", "/mnt/runtime", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/mnt/runtime", NULL, MS_SHARED | MS_REC, NULL) != 0)
        goto mount_failed;
    fprintf(stderr,"ANDROID_RUNTIME_STORAGE_PROPAGATION=private_container_shared\n");

    /* The persistent image is created with a restrictive umask. Restore the
     * Android 7 parent-directory permissions before dropping daemon UIDs. */

    /* Android init creates this directory before installd starts. Match the
     * Android 7 ownership so installd need not exercise the Kindle kernel's
     * post-exec chown path during this bounded container boot. */
    if (chown("/data/misc/user/0", 1000, 9997) != 0 ||
        chmod("/data/misc/user/0", 0750) != 0) goto mount_failed;
    fprintf(stderr, "ZYGOTE_USERNS_DATA_USER_DIR_PREOWNED=1\n");

    struct { const char *path; uid_t uid; gid_t gid; mode_t mode; } system_dirs[] = {
        {"/data", 1000, 1000, 0771},
        {"/data/misc", 1000, 9998, 01771},
        {"/data/misc/user", 0, 0, 0771},
        {"/data/misc/zoneinfo", 1000, 1000, 0775},
        {"/data/misc/zoneinfo/current", 1000, 1000, 0775},
        {"/data/anr", 1000, 1000, 0770},
        {"/data/app", 1000, 1000, 0771},
        {"/data/app-private", 1000, 1000, 0771},
        {"/data/system", 1000, 1000, 0775},
        {"/data/system/heapdump", 1000, 1000, 0700},
        {"/data/system/users", 1000, 1000, 0775},
        {"/data/system/users/0", 1000, 1000, 0775},
        {"/data/system_ce", 1000, 1000, 0770},
        {"/data/system_ce/0", 1000, 1000, 0770},
        {"/data/system_de", 1000, 1000, 0770},
        {"/data/system_de/0", 1000, 1000, 0770},
        {"/data/data", 1000, 1000, 0771},
        {"/data/user", 1000, 1000, 0711},
        {"/data/user_de", 1000, 1000, 0777},
        {"/data/user_de/0", 1000, 1000, 0777},
        {"/data/data/com.android.providers.settings", 1000, 1000, 0771},
        {"/data/data/com.android.providers.settings/databases", 1000, 1000, 0771},
        {"/data/user_de/0/com.android.providers.settings", 1000, 1000, 0771},
        {"/data/user_de/0/com.android.providers.settings/databases", 1000, 1000, 0771},
        {"/data/misc/keystore", 1017, 1017, 0700}
    };
    for (size_t i = 0; i < sizeof(system_dirs) / sizeof(system_dirs[0]); ++i)
        if (chown(system_dirs[i].path, system_dirs[i].uid, system_dirs[i].gid) != 0 ||
            chmod(system_dirs[i].path, system_dirs[i].mode) != 0) goto mount_failed;
    /* Android has no discard-capable block device in this namespace. The host
     * owns image maintenance. A persistent data.img retains this record across
     * boots, so refresh its age too; merely opening an old record lets Android
     * schedule unsupported vold fstrim again after three days. This timestamp
     * defers Android maintenance; it does not claim an actual TRIM occurred. */
    int trim_record = open("/data/system/last-fstrim", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (trim_record < 0 || fchown(trim_record, 1000, 1000) != 0 ||
        fchmod(trim_record, 0644) != 0 || futimens(trim_record, NULL) != 0) {
        if (trim_record >= 0) close(trim_record);
        goto mount_failed;
    }
    close(trim_record);
    fprintf(stderr, "ZYGOTE_USERNS_FSTRIM_RECORD_REFRESHED=1 policy=host_managed_no_block_devices\n");
    fprintf(stderr, "ZYGOTE_USERNS_SYSTEM_DATA_DIRS_PREOWNED=1\n");
    fprintf(stderr, "ZYGOTE_USERNS_APP_INSTALL_DIRS_PREOWNED=1\n");
    if (symlink("/data/data", "/data/user/0") != 0 && errno != EEXIST) goto mount_failed;

    int layout = open("/data/.layout_version", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (layout < 0) goto mount_failed;
    struct stat layout_stat;
    if (fstat(layout, &layout_stat) != 0) { int saved = errno; close(layout); errno = saved; goto mount_failed; }
    if (layout_stat.st_size == 0 && write(layout, "2\n", 2) != 2) {
        int saved = errno; close(layout); errno = saved; goto mount_failed;
    }
    close(layout);
    if (mount("/data/local/tmp", "/tmp", NULL, MS_BIND, NULL) != 0) goto mount_failed;
    fprintf(stderr, "ZYGOTE_USERNS_TMPFS_READY=1\n");
    return 0;

mount_failed:
    fprintf(stderr, "ZYGOTE_USERNS_TMPFS_FAILED errno=%d (%s)\n", errno, strerror(errno));
    return -1;
}

static int copy_apk_probe(void) {
    int input = open("/system/etc/kindlelab-apk-probe.apk", O_RDONLY | O_CLOEXEC);
    if (input < 0) {
        fprintf(stderr, "APK_PROBE_SOURCE_OPEN_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }
    int output = open("/data/local/tmp/kindle-apk-probe.apk",
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (output < 0) {
        int saved = errno;
        close(input);
        fprintf(stderr, "APK_PROBE_DEST_OPEN_FAILED errno=%d (%s)\n", saved, strerror(saved));
        return -1;
    }
    char buffer[16384];
    int failed = 0;
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            failed = errno;
            break;
        }
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset, (size_t)(count - offset));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { failed = errno ? errno : EIO; break; }
            offset += written;
        }
        if (failed) break;
    }
    if (close(input) != 0 && !failed) failed = errno;
    if (close(output) != 0 && !failed) failed = errno;
    if (failed) {
        unlink("/data/local/tmp/kindle-apk-probe.apk");
        fprintf(stderr, "APK_PROBE_COPY_FAILED errno=%d (%s)\n", failed, strerror(failed));
        return -1;
    }
    fprintf(stderr, "APK_PROBE_COPIED_TO_PRIVATE_DATA=1\n");
    return 0;
}


static int write_text(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t length = strlen(value);
    ssize_t written = write(fd, value, length);
    int saved = errno;
    close(fd);
    errno = saved;
    return written == (ssize_t)length ? 0 : -1;
}

static int map_process(pid_t target, uid_t host_uid, gid_t host_gid, const char *proc_root) {
    char path[512], mapping[96];
    snprintf(path, sizeof(path), "%s/%ld/uid_map", proc_root, (long)target);
    snprintf(mapping, sizeof(mapping), "0 %u %u\n", host_uid, ANDROID_ID_MAP_SIZE);
    if (write_text(path, mapping) != 0) return -1;
    snprintf(path, sizeof(path), "%s/%ld/gid_map", proc_root, (long)target);
    snprintf(mapping, sizeof(mapping), "0 %u %u\n", host_gid, ANDROID_ID_MAP_SIZE);
    if (write_text(path, mapping) != 0) return -1;
    return 0;
}

static int map_helper(pid_t target, uid_t host_uid, gid_t host_gid,
                      const char *proc_path, const char *proc_root,
                      int ready_fd, int release_fd) {
    char signal;
    if (read(ready_fd, &signal, 1) != 1 || signal != 'R') return 1;
    close(ready_fd);
    int remounted = 0;
    if (strcmp(proc_root, "/proc") != 0) {
        if (mount(NULL, proc_path, NULL, MS_REMOUNT, NULL) != 0) {
            fprintf(stderr, "ZYGOTE_USERNS_PROC_RW_FAILED errno=%d (%s)\n", errno, strerror(errno));
            return 2;
        }
        remounted = 1;
    }
    int rc = map_process(target, host_uid, host_gid, proc_root);
    int saved = errno;
    if (remounted && mount(NULL, proc_path, NULL, MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_PROC_RO_RESTORE_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 3;
    }
    if (rc != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_MAP_FAILED errno=%d (%s)\n", saved, strerror(saved));
        return 4;
    }
    signal = 'G';
    if (write(release_fd, &signal, 1) != 1) return 5;
    close(release_fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "zygote-namespace-enter: expected command after namespace setup\n");
        return 2;
    }
    uid_t host_uid = geteuid();
    gid_t host_gid = getegid();
    int apk_probe_mode = !strcmp(argv[1], "--chroot-zygote-system-server-apk-probe");
    int system_server_mode = !strcmp(argv[1], "--chroot-zygote-system-server") || apk_probe_mode;
    if (system_server_mode) {
        // Android's display and input threads use URGENT_DISPLAY (-8).
        // Bound this permission to the Android session's system-server tree;
        // don't grant the more aggressive audio priority (-16).
        struct rlimit nice_limit = {28, 28};
        if (setrlimit(RLIMIT_NICE, &nice_limit) == 0) {
            struct rlimit actual;
            if (getrlimit(RLIMIT_NICE, &actual) == 0)
                fprintf(stderr, "ZYGOTE_NICE_RLIMIT soft=%lu hard=%lu min_nice=%ld\n",
                        (unsigned long)actual.rlim_cur, (unsigned long)actual.rlim_max,
                        20L - (long)actual.rlim_cur);
        } else {
            fprintf(stderr, "ZYGOTE_NICE_RLIMIT_FAILED errno=%d (%s)\n", errno, strerror(errno));
        }
    }
    int ready[2], release[2];
    if (pipe(ready) != 0 || pipe(release) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_PIPE_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 9;
    }
    pid_t mapper = fork();
    if (mapper < 0) {
        fprintf(stderr, "ZYGOTE_USERNS_FORK_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 9;
    }
    if (mapper == 0) {
        close(ready[1]); close(release[0]);
        const char *proc_root = "/proc";
        char proc_path[512];
        if (!strcmp(argv[1], "--chroot") || !strcmp(argv[1], "--chroot-zygote-system-server") ||
            !strcmp(argv[1], "--chroot-zygote-system-server-apk-probe")) {
            if (argc < 4 || snprintf(proc_path, sizeof(proc_path), "%s/proc", argv[2]) >= (int)sizeof(proc_path)) _exit(12);
            proc_root = proc_path;
        }
        int rc = map_helper(getppid(), host_uid, host_gid, proc_path, proc_root, ready[0], release[1]);
        _exit(rc);
    }

    close(ready[0]); close(release[1]);
    if (unshare(CLONE_NEWUSER) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_UNSHARE_FAILED errno=%d (%s)\n", errno, strerror(errno));
        close(ready[1]); close(release[0]); kill(mapper, SIGKILL); waitpid(mapper, NULL, 0);
        return 10;
    }
    char signal = 'R';
    if (write(ready[1], &signal, 1) != 1) {
        close(ready[1]); close(release[0]); kill(mapper, SIGKILL); waitpid(mapper, NULL, 0); return 10;
    }
    close(ready[1]);
    if (read(release[0], &signal, 1) != 1 || signal != 'G') {
        close(release[0]); waitpid(mapper, NULL, 0);
        fprintf(stderr, "ZYGOTE_USERNS_MAP_RELEASE_FAILED\n");
        return 10;
    }
    close(release[0]);
    int mapper_status = 0;
    if (waitpid(mapper, &mapper_status, 0) < 0 || !WIFEXITED(mapper_status) || WEXITSTATUS(mapper_status) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_MAP_HELPER_FAILED\n");
        return 10;
    }
    if (getuid() != 0 || getgid() != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_MAP_ID_MISMATCH uid=%u gid=%u\n", getuid(), getgid());
        return 10;
    }
    /* netd needs CAP_NET_ADMIN in the user namespace which OWNS its netns.
     * The outer netns belongs to the host userns, so capabilities gained in
     * this child userns cannot set socket marks or Android routing policy.
     * Create the actual Android netns here; the external relay joins it via
     * this process's userns path. It remains separate from the Kindle netns. */
    if (unshare(CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWNET) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_CHILD_UNSHARE_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 10;
    }
    int netfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct ifreq loopback = {0};
    strcpy(loopback.ifr_name, "lo");
    if (netfd < 0 || ioctl(netfd, SIOCGIFFLAGS, &loopback) != 0) { if (netfd >= 0) close(netfd); return 10; }
    loopback.ifr_flags |= IFF_UP;
    if (ioctl(netfd, SIOCSIFFLAGS, &loopback) != 0) { close(netfd); return 10; }
    close(netfd);
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_PRIVATE_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 10;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "ZYGOTE_USERNS_NNP_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 11;
    }
    fprintf(stderr, "ZYGOTE_USERNS_READY pid=%d uid=%u gid=%u idmap=0-%u mount=private net=isolated-session ipc=private uts=private nnp=%d\n",
            getpid(), getuid(), getgid(), ANDROID_ID_MAP_SIZE - 1,
            prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0));

    int command = 1;
    if (!strcmp(argv[1], "--chroot") || !strcmp(argv[1], "--chroot-zygote-system-server") ||
        apk_probe_mode) {
        if (argc < 4) return 2;
        if (chroot(argv[2]) != 0 || chdir("/") != 0) {
            fprintf(stderr, "ZYGOTE_USERNS_CHROOT_FAILED errno=%d (%s)\n", errno, strerror(errno));
            return 12;
        }
        if (system_server_mode && setup_system_server_mounts() != 0)
            return 13;
        if (apk_probe_mode && chmod("/data/local/tmp", 0777) != 0) {
            fprintf(stderr, "APK_PROBE_TMP_WRITABLE_SETUP_FAILED errno=%d (%s)\n", errno, strerror(errno));
            return 13;
        }
        if (apk_probe_mode)
            fprintf(stderr, "APK_PROBE_TMP_WRITABLE=1 mode=0777 private_tmpfs=1\n");
        if (apk_probe_mode && copy_apk_probe() != 0) return 14;
        command = 3;
    }
    if (apk_probe_mode && setenv("KINDLE_APK_PROBE", "1", 1) != 0) {
        fprintf(stderr, "APK_PROBE_ENV_FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 14;
    }
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 65536) maxfd = 65536;
    for (int fd = 3; fd < maxfd; ++fd) close(fd);
    execvp(argv[command], &argv[command]);
    fprintf(stderr, "ZYGOTE_USERNS_EXEC_FAILED errno=%d (%s)\n", errno, strerror(errno));
    return 127;
}
