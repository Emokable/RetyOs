/* Minimal vold/cryptd stand-in for the container.
 *
 * The real vold never starts listening here (VOLD_LISTEN_FAILED: it needs loop
 * devices and a writable /fstab that fsp cannot provide), so MountService's
 * NativeDaemonConnector gets ECONNREFUSED, mAsecsScanned is never set and any
 * framework path that touches storage blocks -- including `pm install`.
 *
 * art_guard creates both /dev/socket/vold and /dev/socket/cryptd and passes
 * each fd as ANDROID_SOCKET_<name>. Serving only "vold" left cryptd listening to
 * nothing, so CryptdConnector kept reporting ECONNREFUSED and MountService
 * stayed in its connect handshake: boot phase 500 never finished, every
 * Service.onBootPhase after it was skipped, and VrManagerService killed
 * system_server with an NPE on the first setVrMode. Both sockets are answered.
 *
 * This stub speaks just enough of the protocol: read NUL-terminated commands and
 * answer `200 <seq> 0` (success, zero volumes). The sequence number must be
 * echoed -- NativeDaemonConnector dispatches replies by it -- and the third
 * field cannot be omitted because NativeDaemonEvent.parseRawEvent rejects any
 * non-unsolicited reply with fewer than three space-separated fields. See the
 * comment on reply() for the exact source references.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_CMD 1024

/* stderr is not reliably visible in these children, so keep a file record. */
static pthread_mutex_t gLogLock = PTHREAD_MUTEX_INITIALIZER;

static void vlog(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;

    /* stderr is captured with the container log, the file survives log rotation
     * and gives the stub's own record when stderr is not wired up. */
    ssize_t ignored = write(STDERR_FILENO, line, (size_t)n);
    (void)ignored;

    pthread_mutex_lock(&gLogLock);
    int fd = open("/data/local/tmp/vold-stub.log",
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) {
        ssize_t written = write(fd, line, (size_t)n);
        (void)written;
        close(fd);
    }
    pthread_mutex_unlock(&gLogLock);
}

static int open_control_socket(const char *name) {
    char key[64];
    snprintf(key, sizeof(key), "ANDROID_SOCKET_%s", name);
    const char *value = getenv(key);
    if (!value || !*value) return -1;
    int fd = atoi(value);
    if (fd < 0) return -1;
    return fd;
}

static void reply(int fd, const char *code, const char *seq) {
    /* 2026-11-04 第九轮：应答必须是「<code> <命令序号> <message>」三段。
     * 依据（android-7.1.1_r1 源码）：
     *  - NativeDaemonConnector.makeCommand 反序列化时发出 "1 cryptfs getfield ..."，
     *    第一段是自增序号；listenToSocket 用 `mResponseQueue.add(event.getCmdNumber(), event)`
     *    按序号派发，序号对不上就永远匹配不到（表现：60 s 后 NativeDaemonTimeout）。
     *  - NativeDaemonEvent.parseRawEvent 要求非 6xx 应答至少有 3 段（`parsed.length < 3`
     *    直接抛 IllegalArgumentException 并丢弃），所以 message 段不能省，这里填 "0"。
     *  - executeForList 用 `while (event.isClassContinue())` 收列表，200 是终止码，
     *    于是「零个卷」正好用一条 200 表示。
     * 之前固定回 "200 0" 就是把序号写死成 0：只有 0 号命令能匹配，1 号开始全部超时。 */
    size_t length = strlen(code) + strlen(seq);
    char line[128];
    length = snprintf(line, sizeof(line), "%s %s 0", code, seq);
    if (length >= sizeof(line)) length = sizeof(line) - 1;
    line[length] = 0;
    ssize_t sent = write(fd, line, length + 1);
    /* A reply the framework never receives shows up as a NativeDaemonTimeout;
     * record the actual write result so the cause is visible in the log. */
    vlog("VOLD_STUB_REPLY fd=%d bytes=%d errno=%d text=%.20s\n",
         fd, (int)sent, sent < 0 ? errno : 0, line);
}

/* 命令第一段是 NativeDaemonConnector 的自增序号，原样回显；不是纯数字就回 0。 */
static void command_seq(const char *cmd, char *out, size_t out_size) {
    size_t i = 0;
    while (cmd[i] && cmd[i] != ' ' && i + 1 < out_size) {
        if (cmd[i] < '0' || cmd[i] > '9') break;
        out[i] = cmd[i];
        ++i;
    }
    out[i] = 0;
    if (i == 0) snprintf(out, out_size, "0");
}

static const char *answer_for(const char *cmd) {
    (void)cmd;
    /* volume reset / asec list / cryptfs ... all report "success, no volumes";
     * the container genuinely has none. */
    return "200";
}

struct Listener {
    int fd;
    const char *name;
};

struct ClientArgs {
    struct Listener *listener;
    int client;
};

static void serve_client(struct Listener *listener, int client) {
    char buffer[MAX_CMD];
    size_t used = 0;
    /* The listener comes from art_guard and may be non-blocking; an inherited
     * O_NONBLOCK would make the first read return EAGAIN, which this loop treats
     * as a disconnect and closes while the framework is still waiting. */
    int flags = fcntl(client, F_GETFL, 0);
    if (flags >= 0) fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
    vlog("VOLD_STUB_CONNECTED socket=%s fd=%d\n", listener->name, client);
    /* 2026-11-04 第九轮：连接时不再抢先塞一行 "200 0"。那一行会被
     * NativeDaemonConnector 当成「下一条命令的应答」提前消费，造成整条连接
     * 的应答错位；两个 socket 都按命令逐条回就够了。 */
    for (;;) {
        ssize_t n = read(client, buffer + used, sizeof(buffer) - used - 1);
        if (n <= 0) break;
        used += (size_t)n;
        buffer[used] = 0;
        char *start = buffer;
        char *nul;
        while ((nul = memchr(start, 0, (size_t)(buffer + used - start))) != NULL) {
            const char *text = answer_for(start);
            char seq[16];
            command_seq(start, seq, sizeof(seq));
            vlog("VOLD_STUB_CMD socket=%s seq=%s cmd=%.60s reply=%s\n",
                 listener->name, seq, start, text);
            reply(client, text, seq);
            start = nul + 1;
        }
        size_t remaining = (size_t)(buffer + used - start);
        memmove(buffer, start, remaining);
        used = remaining;
        if (used >= sizeof(buffer) - 1) used = 0;
    }
    vlog("VOLD_STUB_DISCONNECTED socket=%s fd=%d\n", listener->name, client);
    close(client);
}

static void *clientThread(void *opaque) {
    struct ClientArgs *args = (struct ClientArgs *)opaque;
    serve_client(args->listener, args->client);
    free(args);
    return NULL;
}

static void *acceptLoop(void *opaque) {
    struct Listener *listener = (struct Listener *)opaque;
    if (listen(listener->fd, 8) != 0) {
        vlog("VOLD_STUB_LISTEN_FAILED socket=%s errno=%d\n", listener->name, errno);
        return NULL;
    }
    vlog("VOLD_STUB_READY socket=%s listening=1 volumes=0 asecs=0\n", listener->name);
    for (;;) {
        int client = accept(listener->fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            vlog("VOLD_STUB_ACCEPT_FAILED socket=%s errno=%d\n", listener->name, errno);
            return NULL;
        }
        /* Serve each client in its own thread: the framework keeps the control
         * connection open for the life of the process. */
        struct ClientArgs *args = (struct ClientArgs *)malloc(sizeof(*args));
        if (args == NULL) {
            close(client);
            continue;
        }
        args->listener = listener;
        args->client = client;
        pthread_t thread;
        if (pthread_create(&thread, NULL, clientThread, args) != 0) {
            vlog("VOLD_STUB_THREAD_FAILED socket=%s errno=%d\n", listener->name, errno);
            close(client);
            free(args);
            continue;
        }
        pthread_detach(thread);
    }
}

int main(void) {
    /* 2026-11-04 第九轮：必须同时伺候 vold 和 cryptd。证据（MountService.java
     * android-7.1.1_r1:458/986-1003）：`mConnectedSignal = new CountDownLatch(2)`，
     * 每有一个 daemon 连上才 countDown 一次；只有两个都连上（count 归零）才会执行
     * `mPms.scanAvailableAsecs()` 并 `mAsecsScanned.countDown()`。只开 vold 时
     * cryptd 永远 ECONNREFUSED → boot phase 500 里的 MountService.systemReady()
     * 卡在 waitForAsecScan()（日志 `MountService Thread main still waiting for
     * mAsecsScanned...`）→ phase 550 永不出现 → 抢跑 am start → VrManagerService
     * 观察者还是 null → `FATAL EXCEPTION IN SYSTEM PROCESS: ActivityManager`。
     * 早先"连 cryptd 会杀 system_server"是因为旧桩对它不应答，getfield 60 s 超时
     * 抛在 handler 线程里；现在两条连接都逐条应答，getfield 会拿到 200 空串。 */
    static const char *names[] = {"vold", "cryptd"};
    struct Listener listeners[2];
    size_t started = 0;

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        int fd = open_control_socket(names[i]);
        if (fd < 0) {
            vlog("VOLD_STUB_NO_CONTROL_SOCKET socket=%s errno=%d\n", names[i], errno);
            continue;
        }
        int dup_fd = dup(fd);
        if (dup_fd < 0) {
            vlog("VOLD_STUB_DUP_FAILED socket=%s errno=%d\n", names[i], errno);
            continue;
        }
        listeners[started].fd = dup_fd;
        listeners[started].name = names[i];
        ++started;
    }
    if (started == 0) return 1;

    for (size_t i = 0; i < started; ++i) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, acceptLoop, &listeners[i]) == 0) {
            pthread_detach(thread);
        } else {
            vlog("VOLD_STUB_THREAD_FAILED socket=%s\n", listeners[i].name);
        }
    }
    for (;;) pause();
    return 0;
}
