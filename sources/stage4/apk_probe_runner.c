#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PACKAGE "org.kindlelab.apkprobe"
#define ACTIVITY "org.kindlelab.apkprobe.ApkProbeActivity"
#define APK_PATH "/data/local/tmp/kindle-apk-probe.apk"
#define DATA_FILE "/data/user_de/0/org.kindlelab.apkprobe/files/probe.txt"
#define TOUCH_FILE "/data/user_de/0/org.kindlelab.apkprobe/files/touch-coordinate.txt"
#define EXIT_FILE "/data/local/tmp/kindle-android-exit"
#define DESKTOP_EXIT_FILE "/data/user_de/0/org.kindlelab.apkprobe/files/kindle-android-exit"
#define USER0_BOOT_READY_FILE "/data/local/tmp/kindlelab-user0-boot-ready"
#define PACKAGES_LIST "/data/system/packages.list"
static uid_t observed_apk_uid;

static int capture_hwtcon_frame(void) {
    const char *capture = getenv("KINDLE_HWTCON_CAPTURE");
    if (!capture || strcmp(capture, "1") != 0) return 1;
    const char *path = "/capture-out/frame.rgba";
    const size_t bytes = 1272U * 1696U * 4U;
    int fb = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fb < 0) { perror("HWTCON_CAPTURE_OPEN_FAILED"); return 0; }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fb, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fb, FBIOGET_FSCREENINFO, &fix) != 0 ||
        var.xres != 1272 || var.yres != 1696 || var.yres_virtual < var.yres ||
        var.xoffset != 0 || var.xres_virtual < var.xres || var.xoffset + var.xres > var.xres_virtual ||
        var.yoffset + var.yres > var.yres_virtual || var.bits_per_pixel != 32 ||
        var.red.offset != 0 || var.green.offset != 8 || var.blue.offset != 16 ||
        var.transp.offset != 24 || fix.line_length != 5088 || fix.smem_len < bytes) {
        printf("HWTCON_CAPTURE_LAYOUT_REJECTED width=%u height=%u xoffset=%u yoffset=%u yvirt=%u bpp=%u pitch=%u smem=%u channels=%u/%u/%u/%u\n",
               var.xres, var.yres, var.xoffset, var.yoffset, var.yres_virtual,
               var.bits_per_pixel, fix.line_length, fix.smem_len,
               var.red.offset, var.green.offset, var.blue.offset, var.transp.offset);
        close(fb);
        return 0;
    }
    const unsigned char *map = mmap(NULL, fix.smem_len, PROT_READ, MAP_SHARED, fb, 0);
    if (map == MAP_FAILED) { perror("HWTCON_CAPTURE_MMAP_FAILED"); close(fb); return 0; }
    int out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out < 0) { perror("HWTCON_CAPTURE_CREATE_FAILED"); munmap((void *)map, fix.smem_len); close(fb); return 0; }
    int ok = 1;
    for (unsigned y = 0; y < var.yres && ok; ++y) {
        size_t left = 1272U * 4U;
        const unsigned char *cursor = map +
            (size_t)(var.yoffset + y) * fix.line_length + (size_t)var.xoffset * 4U;
        while (left) {
            ssize_t n = write(out, cursor, left);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = 0; break; }
            cursor += n;
            left -= (size_t)n;
        }
    }
    if (ok && fsync(out) != 0) ok = 0;
    if (close(out) != 0) ok = 0;
    munmap((void *)map, fix.smem_len);
    close(fb);
    if (!ok) { perror("HWTCON_CAPTURE_WRITE_FAILED"); unlink(path); return 0; }
    printf("HWTCON_FRAME_CAPTURE_PASS=1 width=1272 height=1696 yoffset=%u bytes=%zu read_only=1\n",
           var.yoffset, bytes);
    return 1;
}

static int wait_for_real_touch(void) {
    const char *wait = getenv("KINDLE_HWTCON_WAIT_TOUCH");
    if (!wait || strcmp(wait, "1") != 0) return 1;
    puts("KINDLE_TOUCH_WAIT_BEGIN=1 timeout_seconds=75");
    for (int second = 0; second < 75; ++second) {
        FILE *file = fopen(TOUCH_FILE, "r");
        if (file) {
            char line[256];
            float x = -1.0f, y = -1.0f;
            int width = 0, height = 0;
            int valid = fgets(line, sizeof(line), file) &&
                sscanf(line, "KINDLE_TOUCH x=%f y=%f width=%d height=%d", &x, &y, &width, &height) == 4 &&
                width == 1272 && height == 1696 && x >= 0 && y >= 0 && x < width && y < height;
            fclose(file);
            if (valid) {
                printf("KINDLE_TOUCH_COORDS_PASS=1 x=%.1f y=%.1f width=%d height=%d\n", x, y, width, height);
                sleep(4);
                return 1;
            }
        }
        /* The Activity's app-private data path may not be visible to the
         * container runner on this Android 7 layout. Its tagged log record is
         * already delivered through the private logd, so use that as the
         * read-only coordinate handoff. */
        FILE *logs = popen("/system/bin/logcat -d -s KindleApkProbe:I 2>/dev/null", "r");
        if (logs) {
            char line[512];
            while (fgets(line, sizeof(line), logs)) {
                char *marker = strstr(line, "KINDLE_TOUCH x=");
                if (!marker) continue;
                float x = -1.0f, y = -1.0f;
                int width = 0, height = 0;
                if (sscanf(marker, "KINDLE_TOUCH x=%f y=%f width=%d height=%d",
                           &x, &y, &width, &height) == 4 &&
                    width == 1272 && height == 1696 && x >= 0 && y >= 0 &&
                    x < width && y < height) {
                    printf("KINDLE_TOUCH_COORDS_PASS=1 x=%.1f y=%.1f width=%d height=%d source=logcat\n",
                           x, y, width, height);
                    pclose(logs);
                    sleep(4);
                    return 1;
                }
            }
            pclose(logs);
        }
        sleep(1);
    }
    puts("KINDLE_TOUCH_COORDS_TIMEOUT=1");
    return 0;
}

static int wait_child(pid_t child) {
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 127;
}

static int service_ready(const char *name) {
    pid_t child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        if (setgid(10003) != 0 || setuid(10003) != 0) _exit(126);
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }
        execl("/system/bin/sf-service-check", "sf-service-check", name, (char *)NULL);
        _exit(127);
    }
    return wait_child(child) == 0;
}

static int wait_for_boot_completed(int max_seconds) {
    char last_value[32] = "unavailable";
    for (int i = 0; i < max_seconds; ++i) {
        FILE *property = popen("/system/bin/toybox getprop sys.boot_completed 2>/dev/null", "r");
        if (property) {
            char value[32] = {0};
            int got_value = fgets(value, sizeof(value), property) != NULL;
            pclose(property);
            if (got_value) {
                size_t length = strcspn(value, "\r\n");
                value[length] = '\0';
                snprintf(last_value, sizeof(last_value), "%s", value);
            }
            if (got_value && value[0] == '1') {
                puts("APK_ANDROID_BOOT_COMPLETED=1");
                return 1;
            }
        }
        if (i > 0 && i % 15 == 0) printf("APK_ANDROID_BOOT_WAIT elapsed=%d\n", i);
        sleep(1);
    }
    printf("APK_ANDROID_BOOT_COMPLETED_TIMEOUT seconds=%d last_value=%s\n",
           max_seconds, last_value);
    return 0;
}

static long long command_monotonic_ms(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (long long)now.tv_sec*1000+now.tv_nsec/1000000;
}
static int run_android_command(const char *classpath, char *const argv[]) {
    pid_t child = fork();
    if (child < 0) return 127;
    if (child == 0) {
        if (setenv("CLASSPATH", classpath, 1) != 0) _exit(126);
        execv("/system/bin/app_process32", argv);
        perror("APK_PROBE_APP_PROCESS_EXEC_FAILED");
        _exit(127);
    }
    int install_command = argv[2] && strcmp(argv[2], "com.android.commands.pm.Pm") == 0 &&
                          argv[3] && strcmp(argv[3], "install") == 0;
    int status;
    long long started = command_monotonic_ms();
    int stack_dump_sent = 0;
    int timeout_seconds = install_command ? 90 : 45;
    for (;;) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) return 127;
        long long elapsed = (command_monotonic_ms() - started)/1000;
        if (install_command && !stack_dump_sent && elapsed >= 60) {
            if (kill(child, SIGQUIT) == 0) {
                puts("APK_PM_APP_PROCESS_SIGQUIT_SENT=1");
                stack_dump_sent = 1;
            }
        }
        if (elapsed >= timeout_seconds) {
            printf("APK_PM_COMMAND_TIMEOUT=1 command=%s seconds=%d\n",
                   install_command ? "install" : (argv[3] ? argv[3] : "unknown"),
                   timeout_seconds);
            (void)kill(child, SIGTERM);
            for (int attempt = 0; attempt < 5; ++attempt) {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == child) return 124;
                if (waited < 0 && errno != EINTR) return 127;
                sleep(1);
            }
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            return 124;
        }
        usleep(100000);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 127;
}

/* The Android 7 `am start-user` and `am unlock-user` commands report failure
 * on stderr but can still exit with status 0. Capture their text so bootstrap
 * status reflects ActivityManager's answer instead of the shell process code. */
static int run_android_command_expect_output(const char *classpath,
                                             char *const argv[],
                                             const char *success_text,
                                             const char *failure_text) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return 127;
    pid_t child = fork();
    if (child < 0) { close(pipefd[0]); close(pipefd[1]); return 127; }
    if (child == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0 ||
            setenv("CLASSPATH", classpath, 1) != 0) _exit(126);
        close(pipefd[1]);
        execv("/system/bin/app_process32", argv);
        _exit(127);
    }
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    char output[8192];
    size_t used = 0;
    int eof = 0, status = 0;
    time_t started = time(NULL);
    for (;;) {
        for (;;) {
            char discard[512];
            char *dest = used < sizeof(output) - 1 ? output + used : discard;
            size_t capacity = used < sizeof(output) - 1 ? sizeof(output) - 1 - used : sizeof(discard);
            ssize_t n = read(pipefd[0], dest, capacity);
            if (n > 0) { if (dest == output + used) used += (size_t)n; continue; }
            if (n == 0) eof = 1;
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) eof = 1;
            break;
        }
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child && eof) break;
        if (waited < 0 && errno != EINTR) { close(pipefd[0]); return 127; }
        if (time(NULL) - started >= 45) {
            (void)kill(child, SIGTERM);
            for (int i = 0; i < 5; ++i) {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == child) break;
                usleep(100000);
            }
            if (waited != child) { (void)kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
            close(pipefd[0]);
            puts("ANDROID_USER_COMMAND_TIMEOUT=1 seconds=45");
            return 124;
        }
        usleep(100000);
    }
    close(pipefd[0]);
    output[used] = '\0';
    puts("ANDROID_USER_COMMAND_OUTPUT_BEGIN=1");
    if (used) { fwrite(output, 1, used, stdout); if (output[used - 1] != '\n') putchar('\n'); }
    puts("ANDROID_USER_COMMAND_OUTPUT_END=1");
    int command_rc = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
    int success = success_text && strstr(output, success_text) != NULL;
    int failure = failure_text && strstr(output, failure_text) != NULL;
    printf("ANDROID_USER_COMMAND_RESULT command=%s exit=%d success_text=%d failure_text=%d\n",
           argv[3] ? argv[3] : "unknown", command_rc, success, failure);
    return command_rc == 0 && success && !failure ? 0 : (command_rc ? command_rc : 1);
}

static int process_is_running(void) {
    DIR *proc = opendir("/proc");
    if (!proc) return 0;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(proc)) != NULL) {
        char *end = NULL;
        errno = 0;
        long pid = strtol(entry->d_name, &end, 10);
        if (errno || !end || *end || pid <= 0) continue;
        char path[256];
        if (snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid) >= (int)sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char command[512];
        ssize_t length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0) continue;
        command[length] = '\0';
        for (ssize_t i = 0; i < length; ++i) if (command[i] == '\0') command[i] = ' ';
        if (strstr(command, PACKAGE)) {
            char status_path[256], line[256];
            snprintf(status_path, sizeof(status_path), "/proc/%ld/status", pid);
            FILE *status = fopen(status_path, "r");
            if (status) {
                while (fgets(line, sizeof(line), status)) {
                    unsigned uid;
                    if (sscanf(line, "Uid: %u", &uid) == 1) {
                        observed_apk_uid = (uid_t)uid;
                        break;
                    }
                }
                fclose(status);
            }
            found = 1;
            break;
        }
    }
    closedir(proc);
    return found;
}

static pid_t weread_process_pid(void) {
    DIR *proc = opendir("/proc");
    if (!proc) return -1;
    struct dirent *entry;
    pid_t found = -1;
    while ((entry = readdir(proc)) != NULL) {
        char *end = NULL;
        errno = 0;
        long pid = strtol(entry->d_name, &end, 10);
        if (errno || !end || *end || pid <= 0) continue;
        char path[256];
        if (snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid) >= (int)sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char command[512];
        ssize_t length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0) continue;
        command[length] = '\0';
        for (ssize_t i = 0; i < length; ++i) if (command[i] == '\0') command[i] = ' ';
        if (strstr(command, "com.tencent.weread.eink")) { found = (pid_t)pid; break; }
    }
    closedir(proc);
    return found;
}

static int weread_process_is_running(void) {
    return weread_process_pid() > 0;
}

static int wait_for_weread_process(int seconds) {
    for (int i = 0; i < seconds; ++i) {
        if (weread_process_is_running()) return 1;
        sleep(1);
    }
    return weread_process_is_running();
}

static int unlock_android_system_user_after_boot(int seconds) {
    int elapsed = 0;
    while (elapsed < seconds && access(USER0_BOOT_READY_FILE, F_OK) != 0) {
        sleep(1);
        ++elapsed;
    }
    if (access(USER0_BOOT_READY_FILE, F_OK) != 0) {
        printf("ANDROID_USER0_LATE_BOOT_PHASE_TIMEOUT=1 waited_seconds=%d\n", elapsed);
        return 1;
    }
    printf("ANDROID_USER0_LATE_BOOT_PHASE_READY=1 waited_seconds=%d\n", elapsed);
    int unlock_rc = 127;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        char *unlock_args[] = {"/system/bin/app_process32", "/system/bin",
                              "com.android.commands.am.Am", "unlock-user",
                              "0", "!", "!", NULL};
        unlock_rc = run_android_command_expect_output("/system/framework/am.jar",
                unlock_args, "Success: user unlocked", "Error: could not unlock user");
        printf("ANDROID_USER0_LATE_UNLOCK_ATTEMPT=%d exit=%d\n", attempt, unlock_rc);
        if (unlock_rc == 0) break;
        sleep(2);
    }
    if (unlock_rc == 0) sleep(12);
    return unlock_rc;
}

static int start_weread_after_user_unlock(void) {
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "com.android.commands.am.Am", "start", "-n",
                    "com.tencent.weread.eink/com.tencent.weread.LauncherActivity", NULL};
    puts("WEREAD_LATE_EXPLICIT_LAUNCH_BEGIN=1");
    int rc = run_android_command("/system/framework/am.jar", args);
    printf("WEREAD_LATE_EXPLICIT_LAUNCH_EXIT=%d\n", rc);
    return rc;
}

/* WebView reads this command-line file before creating its compositor.  This
 * is limited to the container test session and lets Chromium try CPU drawing
 * on devices where the Android EGL driver exposes only GLES 1.x. */
static int enable_webview_software_mode(void) {
    static const char command_line[] =
            "webview --disable-gpu --disable-gpu-compositing "
            "--disable-accelerated-2d-canvas --disable-webview-gl-mode\n";
    int fd = open("/data/local/tmp/webview-command-line",
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        printf("WEREAD_WEBVIEW_SOFTWARE_MODE_SETUP_FAILED errno=%d\n", errno);
        return -1;
    }
    (void)fchmod(fd, 0644); /* WebView runs as the application UID. */
    size_t length = sizeof(command_line) - 1;
    ssize_t written = write(fd, command_line, length);
    int close_rc = close(fd);
    if (written != (ssize_t)length || close_rc != 0) {
        (void)unlink("/data/local/tmp/webview-command-line");
        puts("WEREAD_WEBVIEW_SOFTWARE_MODE_SETUP_FAILED write=1");
        return -1;
    }
    puts("WEREAD_WEBVIEW_SOFTWARE_MODE=1 flags=disable-gpu,disable-gpu-compositing,disable-accelerated-2d-canvas,disable-webview-gl-mode");
    return 0;
}

static int wait_for_process(int expected) {
    for (int i = 0; i < 10; ++i) {
        if (process_is_running() == expected) return 1;
        sleep(1);
    }
    return process_is_running() == expected;
}

static int data_marker_present(void) {
    for (int attempt = 0; attempt < 90; ++attempt) {
        pid_t child = fork();
        if (child < 0) return 0;
        if (child == 0) {
            if (observed_apk_uid < 10000 || setgid(observed_apk_uid) != 0 ||
                setuid(observed_apk_uid) != 0) _exit(126);
            char marker[128];
            FILE *file = fopen(DATA_FILE, "r");
            if (!file) _exit(1);
            int present = fgets(marker, sizeof(marker), file) &&
                          strcmp(marker, "ANDROID_DESKTOP_ACTIVITY_CREATED\n") == 0;
            fclose(file);
            _exit(present ? 0 : 1);
        }
        if (wait_child(child) == 0) {
            puts("APK_APP_DATA_MARKER=APK_PROBE_ACTIVITY_CREATED");
            return 1;
        }
        if (!process_is_running()) {
            puts("APK_APP_DATA_MARKER_PROCESS_EXITED=1");
            return 0;
        }
        if (attempt == 0) puts("APK_APP_DATA_MARKER_WAIT_BEGIN=1 max_seconds=90");
        sleep(1);
    }
    puts("APK_APP_DATA_MARKER_TIMEOUT=90");
    return 0;
}

static int package_data_present(void) {
    pid_t child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        if (setgid(1000) != 0 || setuid(1000) != 0) _exit(126);
        struct stat info;
        _exit(stat("/data/user_de/0/" PACKAGE, &info) == 0 && S_ISDIR(info.st_mode) ? 0 : 1);
    }
    return wait_child(child) == 0;
}

static int package_code_present_for(const char *package) {
    pid_t child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        if (setgid(1000) != 0 || setuid(1000) != 0) _exit(126);
        DIR *apps = opendir("/data/app");
        if (!apps) _exit(1);
        struct dirent *entry;
        size_t package_length = strlen(package);
        int found = 0;
        while ((entry = readdir(apps)) != NULL) {
            if (strncmp(entry->d_name, package, package_length) == 0 &&
                entry->d_name[package_length] == '-') {
                found = 1;
                break;
            }
        }
        closedir(apps);
        _exit(found ? 0 : 1);
    }
    return wait_child(child) == 0;
}

static int package_code_present(void) {
    return package_code_present_for(PACKAGE);
}

/* Compare bytes, not versionCode: local desktop builds deliberately share a version. */
static int desktop_apk_matches(void) {
    DIR *apps=opendir("/data/app"); if(!apps)return 0;
    struct dirent *entry;int same=0;
    while((entry=readdir(apps))){
        if(strncmp(entry->d_name,PACKAGE "-",sizeof(PACKAGE))!=0)continue;
        char path[512];snprintf(path,sizeof(path),"/data/app/%s/base.apk",entry->d_name);
        FILE *a=fopen(APK_PATH,"rb"),*b=fopen(path,"rb");
        if(!a||!b){if(a)fclose(a);if(b)fclose(b);continue;}
        unsigned char x[4096],y[4096];size_t nx,ny;same=1;
        do{nx=fread(x,1,sizeof(x),a);ny=fread(y,1,sizeof(y),b);
            if(nx!=ny||memcmp(x,y,nx)){same=0;break;}
        }while(nx);
        if(ferror(a)||ferror(b))same=0;
        fclose(a);fclose(b);if(same)break;
    }
    closedir(apps);return same;
}
static void boot_checkpoint(const char *name){
    struct timespec t;clock_gettime(CLOCK_BOOTTIME,&t);
    printf("ANDROID_PERF stage=%s monotonic_ms=%lld\n",name,(long long)t.tv_sec*1000+t.tv_nsec/1000000);
}

static int repair_package_data_owner(const char *package) {
    FILE *packages = fopen(PACKAGES_LIST, "r");
    if (!packages) {
        printf("APK_DATA_OWNER_LOOKUP_FAILED=1 errno=%d\n", errno);
        return 0;
    }
    char line[1024];
    uid_t uid = (uid_t)-1;
    while (fgets(line, sizeof(line), packages)) {
        char name[256];
        unsigned int parsed_uid;
        if (sscanf(line, "%255s %u", name, &parsed_uid) == 2 &&
            strcmp(name, package) == 0) {
            uid = (uid_t)parsed_uid;
            break;
        }
    }
    fclose(packages);
    if (uid == (uid_t)-1 || uid < 10000) {
        printf("APK_DATA_OWNER_UID_NOT_FOUND package=%s\n", package);
        return 0;
    }
    char owner[32];
    snprintf(owner, sizeof(owner), "%u:%u", (unsigned int)uid, (unsigned int)uid);
    const char *roots[] = {"/data/user_de/0", "/data/data"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", roots[i], package);
        struct stat info;
        if (lstat(path, &info) != 0) {
            if (errno != ENOENT || mkdir(path, 0700) != 0) {
                printf("APK_DATA_OWNER_PATH_FAILED path=%s errno=%d\n", path, errno);
                return 0;
            }
        }
        pid_t child = fork();
        if (child < 0) return 0;
        if (child == 0) {
            execl("/system/bin/toybox", "toybox", "chown", "-R", owner, path, (char *)NULL);
            _exit(127);
        }
        if (wait_child(child) != 0) {
            printf("APK_DATA_OWNER_CHOWN_FAILED path=%s uid=%u\n", path, (unsigned int)uid);
            return 0;
        }
        if (chmod(path, 0700) != 0) {
            printf("APK_DATA_OWNER_MODE_FAILED path=%s errno=%d\n", path, errno);
            return 0;
        }
    }
    printf("APK_DATA_OWNER_REPAIRED package=%s uid=%u\n", package, (unsigned int)uid);
    return 1;
}

static int storage_probe_make_dir(const char *path) {
    if (mkdir(path, 0771) == 0 || errno == EEXIST) {
        struct stat info;
        return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
    }
    return 0;
}

static int storage_probe_roundtrip(const char *directory, const char *kind) {
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/.kindlelab-%s-%ld",
                     directory, kind, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    static const char expected[] = "KINDLELAB_WEREAD_STORAGE_PROBE\n";
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { dprintf(STDOUT_FILENO, "WEREAD_STORAGE_CREATE_FAIL kind=%s errno=%d\n", kind, errno); return 0; }
    int ok = write(fd, expected, sizeof(expected) - 1) == (ssize_t)(sizeof(expected) - 1) &&
             fsync(fd) == 0 && lseek(fd, 0, SEEK_SET) == 0;
    char actual[sizeof(expected)];
    ssize_t count = ok ? read(fd, actual, sizeof(actual) - 1) : -1;
    if (count != (ssize_t)(sizeof(expected) - 1) || memcmp(actual, expected, sizeof(expected) - 1)) ok = 0;
    if (close(fd) != 0) ok = 0;
    if (unlink(path) != 0) ok = 0;
    if (!ok) dprintf(STDOUT_FILENO, "WEREAD_STORAGE_ROUNDTRIP_FAIL kind=%s errno=%d\n", kind, errno);
    return ok;
}

static int probe_weread_external_storage(void) {
    FILE *packages = fopen(PACKAGES_LIST, "r");
    if (!packages) { printf("WEREAD_EXTERNAL_UID_LOOKUP_FAIL errno=%d\n", errno); return 0; }
    char line[1024], name[256];
    unsigned int uid = 0;
    int found = 0;
    while (fgets(line, sizeof(line), packages)) {
        if (sscanf(line, "%255s %u", name, &uid) == 2 &&
            strcmp(name, "com.tencent.weread.eink") == 0) { found = 1; break; }
    }
    fclose(packages);
    if (!found || uid < 10000) {
        puts("WEREAD_EXTERNAL_UID_LOOKUP_FAIL=1");
        return 0;
    }
    pid_t child = fork();
    if (child < 0) { printf("WEREAD_EXTERNAL_PROBE_FORK_FAIL errno=%d\n", errno); return 0; }
    if (child == 0) {
        if (setgroups(0, NULL) != 0 || setgid((gid_t)uid) != 0 || setuid((uid_t)uid) != 0) {
            dprintf(STDOUT_FILENO, "WEREAD_EXTERNAL_IDENTITY_FAIL uid=%u errno=%d\n", uid, errno);
            _exit(126);
        }
        static const char *directories[] = {
            "/storage/emulated/0/Android",
            "/storage/emulated/0/Android/data",
            "/storage/emulated/0/Android/data/com.tencent.weread.eink",
            "/storage/emulated/0/Android/data/com.tencent.weread.eink/files",
            "/storage/emulated/0/Android/data/com.tencent.weread.eink/cache"
        };
        for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
            if (!storage_probe_make_dir(directories[i])) {
                dprintf(STDOUT_FILENO, "WEREAD_EXTERNAL_MKDIR_FAIL path=%s errno=%d\n",
                        directories[i], errno);
                _exit(1);
            }
        }
        int files_ok = storage_probe_roundtrip(directories[3], "files");
        int cache_ok = storage_probe_roundtrip(directories[4], "cache");
        if (files_ok && cache_ok) {
            dprintf(STDOUT_FILENO, "WEREAD_EXTERNAL_FILES_RW_PASS uid=%u files=1 cache=1 cleanup=1\n", uid);
            _exit(0);
        }
        _exit(1);
    }
    int status = wait_child(child);
    if (status != 0) printf("WEREAD_EXTERNAL_STORAGE_PROBE_FAILED status=%d\n", status);
    return status == 0;
}

static void dump_directory(const char *path) {
    pid_t child = fork();
    if (child < 0) {
        printf("APK_FS_DIR path=%s fork=0 errno=%d\n", path, errno);
        return;
    }
    if (child == 0 && (setgid(1000) != 0 || setuid(1000) != 0)) _exit(126);
    if (child > 0) {
        int status = wait_child(child);
        if (status != 0) printf("APK_FS_DIR path=%s open=0 status=%d\n", path, status);
        return;
    }
    DIR *dir = opendir(path);
    if (!dir) {
        printf("APK_FS_DIR path=%s open=0 errno=%d\n", path, errno);
        _exit(1);
    }
    printf("APK_FS_DIR path=%s open=1", path);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 40) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            printf(" item=%s", entry->d_name);
            ++count;
        }
    }
    printf(" count_at_least=%d\n", count);
    closedir(dir);
    _exit(0);
}

static void dump_weread_tombstones(void) {
    DIR *directory = opendir("/data/tombstones");
    if (!directory) {
        printf("WEREAD_TOMBSTONE_DIRECTORY_UNAVAILABLE errno=%d\n", errno);
        return;
    }
    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "tombstone_", 10) != 0) continue;
        char path[512];
        if (snprintf(path, sizeof(path), "/data/tombstones/%s", entry->d_name) >=
                (int)sizeof(path)) continue;
        FILE *file = fopen(path, "r");
        if (!file) continue;
        char line[2048], header[8192];
        size_t used = 0;
        while (used + 1 < sizeof(header) && fgets(line, sizeof(line), file)) {
            size_t length = strlen(line);
            if (length > sizeof(header) - used - 1) length = sizeof(header) - used - 1;
            memcpy(header + used, line, length);
            used += length;
            if (strchr(line, '\n')) break;
        }
        header[used] = '\0';
        if (!strstr(header, "com.tencent.weread.eink")) {
            fclose(file);
            continue;
        }
        printf("WEREAD_TOMBSTONE_BEGIN file=%s\n", entry->d_name);
        rewind(file);
        int lines = 0;
        while (lines < 100 && fgets(line, sizeof(line), file)) {
            fputs(line, stdout);
            if (!strchr(line, '\n')) putchar('\n');
            ++lines;
        }
        printf("WEREAD_TOMBSTONE_END lines=%d\n", lines);
        fclose(file);
        if (++found >= 3) break;
    }
    closedir(directory);
    if (!found) puts("WEREAD_TOMBSTONES_NONE=1");
}

static int run_install(void) {
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "com.android.commands.pm.Pm", "install", "-r", APK_PATH, NULL};
    return run_android_command("/system/framework/pm.jar", args);
}

static void dump_user_lifecycle_logs(const char *checkpoint) {
    FILE *logs = popen("/system/bin/logcat -d -v brief -s ActivityManager:I UserController:D StorageManager:I 2>/dev/null", "r");
    if (!logs) {
        printf("ANDROID_USER_LIFECYCLE_LOG_UNAVAILABLE checkpoint=%s\n", checkpoint);
        return;
    }
    printf("ANDROID_USER_LIFECYCLE_LOG_BEGIN checkpoint=%s\n", checkpoint);
    char line[1024];
    int emitted = 0;
    while (fgets(line, sizeof(line), logs) && emitted < 80) {
        if (!strstr(line, "user") && !strstr(line, "User") &&
            !strstr(line, "unlock") && !strstr(line, "Unlock") &&
            !strstr(line, "boot") && !strstr(line, "Boot") &&
            !strstr(line, "key") && !strstr(line, "Key")) continue;
        printf("ANDROID_USER_LIFECYCLE_LOG %s", line);
        ++emitted;
    }
    pclose(logs);
    printf("ANDROID_USER_LIFECYCLE_LOG_END checkpoint=%s count=%d\n", checkpoint, emitted);
}

static int ensure_android_system_user_unlocked(int wait_for_user_boot) {
    (void)unlink(USER0_BOOT_READY_FILE);
    char *start_args[] = {"/system/bin/app_process32", "/system/bin",
                          "com.android.commands.am.Am", "start-user", "0", NULL};
    int start_rc = run_android_command_expect_output("/system/framework/am.jar", start_args,
                                                     "Success: user started",
                                                     "Error: could not start user");
    printf("ANDROID_USER_START_EXIT=%d\n", start_rc);
    if (start_rc != 0) {
        puts("ANDROID_USER_UNLOCK_SKIPPED=1 reason=user-start-failed");
        return start_rc;
    }
    if (!wait_for_user_boot) {
        puts("ANDROID_USER_UNLOCK_DEFERRED=normal_desktop_bootstrap");
        return 0;
    }
    puts("ANDROID_USER_START_WAIT_BEGIN=1 signal=ActivityManager.Finishing_user_boot_0 timeout_seconds=120");
    int boot_ready = 0;
    for (int second = 0; second < 120; ++second) {
        if (access(USER0_BOOT_READY_FILE, F_OK) == 0) {
            printf("ANDROID_USER0_BOOT_PHASE_READY=1 elapsed_seconds=%d\n", second);
            boot_ready = 1;
            break;
        }
        sleep(1);
    }
    if (!boot_ready) {
        puts("ANDROID_USER0_BOOT_PHASE_TIMEOUT=1 continuing_minimal_android=1");
        dump_user_lifecycle_logs("user-boot-timeout");
        return 1;
    }
    sleep(2);
    int unlock_rc = 127;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        char *unlock_args[] = {"/system/bin/app_process32", "/system/bin",
                              "com.android.commands.am.Am", "unlock-user",
                              "0", "!", "!", NULL};
        unlock_rc = run_android_command_expect_output("/system/framework/am.jar", unlock_args,
                                                      "Success: user unlocked",
                                                      "Error: could not unlock user");
        printf("ANDROID_USER_UNLOCK_ATTEMPT=%d exit=%d\n", attempt, unlock_rc);
        if (unlock_rc == 0) break;
        sleep(2);
    }
    printf("ANDROID_USER_UNLOCK_REQUEST_PASS=%d\n", unlock_rc == 0);
    /* unlock-user reports that the request was accepted, not that UserController
     * has finished the asynchronous RUNNING_UNLOCKING transition. Give Android
     * time to finish before PackageManager and ActivityManager probes run. */
    sleep(12);
    dump_user_lifecycle_logs("after-unlock-request");
    return unlock_rc;
}

static int dump_weread_package_state(int api_only);

static int set_weread_launcher_state(const char *state) {
    char component[] = "com.tencent.weread.eink/com.tencent.weread.LauncherActivity";
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "com.android.commands.pm.Pm", (char *)state, component, NULL};
    return run_android_command("/system/framework/pm.jar", args);
}

static int grant_weread_storage_permissions(void) {
    static const char *permissions[] = {
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE"
    };
    int ok = 1;
    for (unsigned i = 0; i < sizeof(permissions) / sizeof(permissions[0]); ++i) {
        char *args[] = {"/system/bin/app_process32", "/system/bin",
                        "com.android.commands.pm.Pm", "grant",
                        "com.tencent.weread.eink", (char *)permissions[i], NULL};
        int rc = run_android_command("/system/framework/pm.jar", args);
        printf("WEREAD_PERMISSION_GRANT permission=%s exit=%d\n", permissions[i], rc);
        if (rc != 0) ok = 0;
    }
    printf("WEREAD_STORAGE_PERMISSION_GRANT_PASS=%d\n", ok);
    return ok;
}

static int run_start(void) {
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "com.android.commands.am.Am", "start", "-n",
                    PACKAGE "/." "ApkProbeActivity", NULL};
    return run_android_command("/system/framework/am.jar", args);
}

static int dump_weread_package_state(int api_only) {
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "org.kindlelab.diagnostics.PackageServiceDump",
                    "com.tencent.weread.eink", api_only ? "api-only" : NULL, NULL};
    return run_android_command("/system/framework/kindlelab-package-dump.jar", args);
}

static void dump_weread_process_snapshot(void) {
    DIR *proc = opendir("/proc");
    if (!proc) { puts("WEREAD_PROCESS_SNAPSHOT_OPEN_FAILED=1"); return; }
    puts("WEREAD_PROCESS_SNAPSHOT_BEGIN=1");
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        char *end = NULL;
        errno = 0;
        long pid = strtol(entry->d_name, &end, 10);
        if (errno || !end || *end || pid <= 0) continue;
        char path[256], command[1024];
        if (snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid) >= (int)sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0) continue;
        command[length] = '\0';
        for (ssize_t i = 0; i < length; ++i) if (command[i] == '\0') command[i] = ' ';
        if (!strstr(command, "com.tencent") && !strstr(command, "weread") &&
            !strstr(command, "LauncherActivity")) continue;
        char status_path[256], status_line[256] = "state=unknown uid=unknown";
        snprintf(status_path, sizeof(status_path), "/proc/%ld/status", pid);
        FILE *status = fopen(status_path, "r");
        if (status) {
            char line[256];
            while (fgets(line, sizeof(line), status)) {
                if (!strncmp(line, "State:", 6) || !strncmp(line, "Uid:", 4)) {
                    size_t used = strlen(status_line);
                    if (used + strlen(line) + 2 < sizeof(status_line)) {
                        status_line[used++] = ' ';
                        strcpy(status_line + used, line);
                    }
                }
            }
            fclose(status);
        }
        printf("WEREAD_PROCESS_SNAPSHOT pid=%ld%s cmd=%s\n", pid, status_line, command);
    }
    closedir(proc);
    puts("WEREAD_PROCESS_SNAPSHOT_END=1");
}

static int run_uninstall(void);

static int configure_session_dns(void) {
    pid_t child = fork();
    if (child < 0) {
        printf("APK_DNS_CONFIG_FORK_FAILED errno=%d\n", errno);
        return 127;
    }
    if (child == 0) {
        execl("/system/bin/ndc", "ndc", "resolver", "setnetdns", "0", "",
              "10.0.2.3", (char *)NULL);
        _exit(127);
    }
    int status = wait_child(child);
    if (status != 0) printf("APK_DNS_CONFIG_FAILED status=%d\n", status);
    else puts("APK_DNS_CONFIG_PASS netid=0 server=10.0.2.3");
    return status;
}

static void start_system_controls(void) {
    if (access("/system/framework/kindle-system-controls.dex", R_OK) != 0) return;
    unlink("/data/local/tmp/kindle-controls-ready");
    unlink("/data/local/tmp/kindle-display-full-refresh");
    unlink("/data/local/tmp/kindle-display-mode");
    pid_t pid = fork();
    if (pid == 0) {
        gid_t groups[] = {1000, 3003, 3004};
        if (setgroups(3, groups) || setgid(1000) || setuid(1000)) _exit(126);
        setenv("CLASSPATH", "/system/framework/kindle-system-controls.dex", 1);
        execl("/system/bin/app_process32", "app_process32", "/system/bin", "KindleSystemControls", (char*)NULL);
        _exit(127);
    }
    printf("SYSTEM_CONTROLS_PROCESS=%d\n", pid);
    for (int i=0; i<120; ++i) {
        if (access("/data/local/tmp/kindle-controls-ready", F_OK) == 0) break;
        usleep(100000);
    }
}

static int run_uninstall(void) {
    char *args[] = {"/system/bin/app_process32", "/system/bin",
                    "com.android.commands.pm.Pm", "uninstall", PACKAGE, NULL};
    return run_android_command("/system/framework/pm.jar", args);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    boot_checkpoint("runner-start");
    puts("APK_PROBE_RUNNER_STARTED scope=private-mount-pid-ipc-isolated-session-network");
    const char *session = getenv("KINDLE_ANDROID_SESSION");
    int persistent_session = session && strcmp(session, "1") == 0;
    struct stat apk;
    if (stat(APK_PATH, &apk) != 0 || !S_ISREG(apk.st_mode) || apk.st_size <= 0 || apk.st_size > 1024 * 1024) {
        puts("APK_PROBE_INPUT_INVALID");
        return 20;
    }
    (void)unlink(TOUCH_FILE);
    (void)unlink(EXIT_FILE);
    (void)unlink(DESKTOP_EXIT_FILE);

    int ready = 0;
    /* First boot on the Kindle's slow storage can take longer than the short
     * disposable APK probe window. Keep the ordinary probe bounded, while a
     * persistent session gets time for PackageManager and ActivityManager to
     * finish their first-boot scans. */
    const int service_wait_seconds = persistent_session ? 180 : 42;
    for (int i = 0; i < service_wait_seconds; ++i) {
        if (service_ready("package") && service_ready("activity") && service_ready("mount")) { ready = 1; break; }
        if (persistent_session && i > 0 && i % 15 == 0) {
            printf("APK_FRAMEWORK_WAIT elapsed=%d package=%d activity=%d mount=%d\n",
                   i, service_ready("package"), service_ready("activity"), service_ready("mount"));
        }
        sleep(1);
    }
    if (!ready) {
        printf("APK_SERVICE_STATUS package=%d activity=%d mount=%d\n",
               service_ready("package"), service_ready("activity"), service_ready("mount"));
        puts("APK_FRAMEWORK_SERVICES_NOT_READY");
        return 21;
    }
    puts("APK_FRAMEWORK_SERVICES_READY=package,activity,mount");
    if (persistent_session) (void)configure_session_dns();
    /* Keep an identical, previously tested desktop installed. Updating through
     * PackageManager preserves its data and avoids an extra uninstall JVM. */
    int reuse_desktop=persistent_session && desktop_apk_matches() && package_data_present();
    printf("ANDROID_DESKTOP_REUSE=%d\n",reuse_desktop);
    boot_checkpoint("framework-ready");
    /* The property service does not publish sys.boot_completed in this isolated
     * session even after package/activity/mount are usable. Avoid a known
     * 90-second false wait for the user-controlled persistent desktop. */
    int boot_complete = persistent_session ? 0 : wait_for_boot_completed(42);
    if (!boot_complete && !persistent_session) return 21;
    if (!boot_complete) {
        if (persistent_session)
            puts("APK_BOOT_GATE_OPTIONAL_CONTINUE=package,activity,mount-ready property_gate_skipped=persistent-session");
        else
            puts("APK_BOOT_GATE_OPTIONAL_CONTINUE=package,activity,mount-ready");
    }
    if (persistent_session)
        puts("ANDROID_SDCARD_DAEMON_START=vold-volume-mount");

    const char *auto_weread = getenv("KINDLELAB_AUTOLAUNCH_WEREAD");
    int weread_test_requested = auto_weread && strcmp(auto_weread, "1") == 0;
    puts("WEREAD_INSTALL_MODEL=user_app_package_installer");
    puts("WEREAD_INSTALL_HINT=copy_apk_to_AndroidFiles_and_open_with_Files_app");
    if (persistent_session && auto_weread && strcmp(auto_weread, "1") == 0) {
        puts("WEREAD_PRE_UNLOCK_PACKAGE_API_BEGIN=1");
        printf("WEREAD_PRE_UNLOCK_PACKAGE_API_EXIT=%d\n",
               dump_weread_package_state(1));
    }
    if (persistent_session) {
        puts("ANDROID_USER_UNLOCK_BOOTSTRAP_BEGIN=1 user=0");
        (void)ensure_android_system_user_unlocked(weread_test_requested);
    }
    if (persistent_session && weread_test_requested)
        (void)repair_package_data_owner("com.tencent.weread.eink");
    /* On a re-install, Android 7 may retain the package's old denied runtime
     * storage permissions. Grant them inside this private Android instance
     * before any app-side external-storage access. */
    if (persistent_session && weread_test_requested) {
        (void)grant_weread_storage_permissions();
        (void)probe_weread_external_storage();
    } else if (persistent_session) {
        puts("WEREAD_RUNTIME_CHECKS_SKIPPED=normal_android_desktop_start");
    }

    int weread_launcher_enabled_for_test = 0;
    if (persistent_session && weread_test_requested) {
        puts("WEREAD_POST_UNLOCK_PACKAGE_API_BEGIN=1");
        printf("WEREAD_POST_UNLOCK_PACKAGE_API_EXIT=%d\n",
               dump_weread_package_state(1));
        puts("WEREAD_LAUNCHER_ENABLE_TEST_BEGIN=1");
        int enable_rc = set_weread_launcher_state("enable");
        printf("WEREAD_LAUNCHER_ENABLE_TEST_EXIT=%d\n", enable_rc);
        weread_launcher_enabled_for_test = enable_rc == 0;
    }

    puts("APK_PM_INSTALL_BEGIN=1");
    int install_rc = reuse_desktop ? 0 : run_install();
    boot_checkpoint("desktop-installed");
    printf("APK_PM_INSTALL_EXIT=%d\n", install_rc);
    dump_directory("/data/app");
    dump_directory("/data/user_de/0");
    dump_directory("/data/user/0");
    dump_directory("/data/data");
    if (install_rc != 0 || !package_code_present() || !package_data_present()) {
        printf("APK_INSTALL_STATE code=%d data=%d\n", package_code_present(), package_data_present());
        if (install_rc == 124) {
            puts("APK_INSTALL_CLEANUP_DEFERRED=1 reason=bounded_pm_timeout_namespace_shutdown_will_reconcile");
        } else if (package_code_present()) {
            (void)run_uninstall();
        }
        if (weread_launcher_enabled_for_test)
            printf("WEREAD_LAUNCHER_ENABLE_TEST_RESTORE_EXIT=%d\n",
                   set_weread_launcher_state("default-state"));
        return 22;
    }
    puts("APK_PM_INSTALL_PASS=1");
    (void)repair_package_data_owner(PACKAGE);
    /* Permission grants are batched in the existing controls VM. */

    int package_dump_rc = (!persistent_session || weread_test_requested) ? dump_weread_package_state(0) : 0;
    printf("WEREAD_PACKAGE_SERVICE_DUMP_EXIT=%d\n", package_dump_rc);

    if (persistent_session) { (void)enable_webview_software_mode(); start_system_controls(); }
    boot_checkpoint("controls-ready");
    int start_rc = run_start();
    boot_checkpoint("desktop-start-requested");
    printf("APK_AM_START_EXIT=%d\n", start_rc);
    int activity_process_started = start_rc == 0 && wait_for_process(1);
    int app_ready_marker = 0;
    int launched;
    if (persistent_session) {
        /* A persistent desktop session is user-controlled. Keep it alive once
         * ActivityManager accepted the launch and the app process appeared;
         * the app-private marker remains diagnostic only. */
        launched = activity_process_started;
        printf("APK_ACTIVITY_PROCESS_LAUNCH_PASS=%d\n", launched);
        puts("APK_APP_READY_MARKER_GATE=disabled persistent_session=1");
    } else {
        app_ready_marker = activity_process_started && data_marker_present();
        launched = app_ready_marker;
    }
    printf("APK_ACTIVITY_LAUNCH_PASS=%d\n", launched);
    printf("APK_PROCESS_PRESENT_BEFORE_UNINSTALL=%d\n", process_is_running());

    int weread_stable = 0;
    if (launched && persistent_session && auto_weread && strcmp(auto_weread, "1") == 0) {
        puts("WEREAD_AUTO_LAUNCH_BEGIN=1");
        fflush(stdout);
        int webview_software_mode = enable_webview_software_mode() == 0;
        int unlock_rc = unlock_android_system_user_after_boot(120);
        printf("ANDROID_USER0_LATE_UNLOCK_PASS=%d\n", unlock_rc == 0);
        int weread_started = 0;
        if (unlock_rc == 0) {
            (void)dump_weread_package_state(1);
            (void)start_weread_after_user_unlock();
            weread_started = wait_for_weread_process(45);
        }
        printf("WEREAD_PROCESS_LAUNCH_PASS=%d\n", weread_started);
        if (!weread_started) dump_weread_process_snapshot();
        if (weread_started) {
            pid_t launched_pid = weread_process_pid();
            printf("WEREAD_PROCESS_PID=%ld\n", (long)launched_pid);
            weread_stable = launched_pid > 0;
            for (int second = 0; weread_stable && second < 60; ++second) {
                sleep(1);
                pid_t observed_pid = weread_process_pid();
                if (observed_pid != launched_pid) {
                    weread_stable = 0;
                    printf("WEREAD_PROCESS_CHANGED_AT=%d old_pid=%ld new_pid=%ld\n",
                           second + 1, (long)launched_pid, (long)observed_pid);
                }
            }
            printf("WEREAD_PROCESS_STABLE_60S=%d\n", weread_stable);
            if (!weread_stable) dump_weread_tombstones();
        }
        if (webview_software_mode) {
            (void)unlink("/data/local/tmp/webview-command-line");
            puts("WEREAD_WEBVIEW_SOFTWARE_MODE_CLEANED=1");
        }
        fflush(stdout);
    }
    if (weread_launcher_enabled_for_test && !weread_stable) {
        int restore_rc = set_weread_launcher_state("default-state");
        printf("WEREAD_LAUNCHER_ENABLE_TEST_RESTORE_EXIT=%d\n", restore_rc);
    }

    int session_exit_seen = 0;
    int session_timed_out = 0;
    if (launched && persistent_session) {
        puts("ANDROID_SESSION_WAITING_FOR_EXIT_REQUEST=1 timeout=none");
        fflush(stdout);
        for (;;) {
            if (access(EXIT_FILE, F_OK) == 0 || access(DESKTOP_EXIT_FILE, F_OK) == 0) {
                session_exit_seen = 1;
                printf("ANDROID_EXIT_REQUEST_SOURCE=%s\n",
                       access(DESKTOP_EXIT_FILE, F_OK) == 0 ? "desktop-private-file" : "maintenance-file");
                puts("KINDLE_ANDROID_EXIT_REQUEST=1");
                break;
            }
            sleep(1);
        }
    }

    if (!session_exit_seen && !session_timed_out && launched && getenv("KINDLE_HWTCON_CAPTURE") &&
        strcmp(getenv("KINDLE_HWTCON_CAPTURE"), "1") == 0) {
        if (!wait_for_real_touch())
            puts("KINDLE_TOUCH_COORDINATE_HANDOFF_MISSED=1; continuing read-only frame capture");
        puts("APK_DISPLAY_HOLD_BEGIN=1 seconds=4");
        sleep(4);
        if (!capture_hwtcon_frame()) {
            (void)run_uninstall();
            return 24;
        }
        const char *hold = getenv("KINDLE_HWTCON_HOLD_SECONDS");
        if (hold && *hold) {
            char *end = NULL;
            long seconds = strtol(hold, &end, 10);
            if (end != hold && *end == '\0' && seconds >= 1 && seconds <= 60) {
                printf("HWTCON_ANDROID_HOLD_BEGIN=1 seconds=%ld\n", seconds);
                sleep((unsigned int)seconds);
                puts("HWTCON_ANDROID_HOLD_END=1");
            }
        }
    }

    if (persistent_session) {
        int code_retained = package_code_present();
        int data_retained = package_data_present();
        int process_running_at_handoff = process_is_running();
        int retained = code_retained && data_retained;
        printf("ANDROID_DESKTOP_RETAINED_IN_DATA_IMAGE=%d code=%d data=%d process_running_at_handoff=%d\n",
               retained, code_retained, data_retained, process_running_at_handoff);
        return launched && session_exit_seen && retained ? 0 : 23;
    }

    int uninstall_rc = run_uninstall();
    printf("APK_PM_UNINSTALL_EXIT=%d\n", uninstall_rc);
    int removed = uninstall_rc == 0 && !package_code_present() && !package_data_present() && wait_for_process(0);
    printf("APK_PM_UNINSTALL_PASS=%d\n", removed);
    (void)unlink(TOUCH_FILE);
    (void)unlink(EXIT_FILE);

    return launched && removed && (session_exit_seen || !session || strcmp(session, "1") != 0) ? 0 : 23;
}
