#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
# r101 定位：controller 启动即执行 session-freezer prepare（linux.rs:404，全容器进程迁移进
# freezer cgroup），boot 期调用必卡死（WARM=1 两轮 boot 死于 PMS 阶段→180s recovery；
# WARM=0 正常 ×2；稳态手动 prepare 无害）。因此延迟到 boot 真正通过（LAUNCH_PASS 出现在
# result.txt）再 exec controller：prepare 落在已证无害的稳态。
stage="$2"
if [ -n "${stage:-}" ] && [ -d "$stage" ]; then
    i=0
    while [ "$i" -lt 300 ]; do
        grep -q "APK_ACTIVITY_PROCESS_LAUNCH_PASS=1" "$stage/result.txt" 2>/dev/null && break
        # owner（start.sh）没了就别等了：会话已死，controller 无可保，直接退。
        kill -0 "$1" 2>/dev/null || exit 0
        sleep 2
        i=$((i + 2))
    done
fi
if [ -x "$BASE/session-controller" ] && [ -f "$BASE/session-controller.sha256" ];then
    (cd "$BASE" && sha256sum -c session-controller.sha256) || exit 8
    exec "$BASE/session-controller" run "$@"
fi
exec /bin/sh "$BASE/warm-session.legacy.sh" "$@"
