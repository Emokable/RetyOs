#!/bin/sh
set -u

BASE=/mnt/us/kindle-android
LOCK=$BASE/session.lock
MARKER=$BASE/framework-stopped
LOG=$BASE/framework-watchdog.log
LAUNCHER=${1:-}

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"; }
lock_owner() { cat "$LOCK/pid" 2>/dev/null || :; }
launcher_is_alive() {
    pid=$1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ -r "/proc/$pid/cmdline" ] || return 1
    cmd=$(tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null || :)
    case "$cmd" in *"$BASE/start.sh"*) return 0;; esac
    return 1
}
controller_is_alive() {
    pid=$(cat "$BASE/warm-controller.pid" 2>/dev/null || :)
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    kill -0 "$pid" 2>/dev/null || return 1
    [ -r "/proc/$pid/cmdline" ] || return 1
    tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q 'session-controller'
}
read_valid_stage() {
    stage=$(sed -n '1p' "$BASE/boot-progress.stage" 2>/dev/null || :)
    case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) stage=; return 1;; esac
    suffix=${stage#/mnt/us/kindle-a7-art.}
    case "$suffix" in *[!A-Za-z0-9]*|'') stage=; return 1;; esac
    [ -d "$stage" ] && [ ! -L "$stage" ]
}
process_rooted_in_stage() {
    candidate=$1
    for proc in /proc/[0-9]*; do
        [ -d "$proc" ] || continue
        root=$(readlink "$proc/root" 2>/dev/null || :)
        [ "$root" = "$candidate/rootfs" ] && return 0
        cwd=$(readlink "$proc/cwd" 2>/dev/null || :)
        case "$cwd" in "$candidate"|"$candidate"/*) return 0;; esac
    done
    return 1
}
stage_mounted_in_any_namespace() {
    candidate=$1
    for info in /proc/[0-9]*/mountinfo; do
        grep -Fq "$candidate" "$info" 2>/dev/null && return 0
    done
    return 1
}
request_android_exit() {
    candidate=$1
    found=0
    for proc in /proc/[0-9]*; do
        [ -d "$proc" ] || continue
        root=$(readlink "$proc/root" 2>/dev/null || :)
        [ "$root" = "$candidate/rootfs" ] || continue
        found=1
        exit_file="$proc/root/data/local/tmp/kindle-android-exit"
        # The retained/native handoff detaches Android's /data mount. In that
        # state /data/local/tmp may not exist at all, so recreate only this
        # transient request directory inside the isolated container root.
        if [ ! -d "$proc/root/data/local/tmp" ]; then
            mkdir -p "$proc/root/data/local/tmp" 2>/dev/null || :
        fi
        if printf 'watchdog-recovery\n' > "$exit_file" 2>/dev/null; then
            log "WATCHDOG_EXIT_REQUESTED pid=${proc#/proc/} stage=$candidate"
        fi
    done
    [ "$found" = 1 ]
}
remove_stage_if_empty() {
    candidate=$1
    [ -d "$candidate" ] || return 0
    [ ! -e "$candidate/hwtcon-backup-ready" ] || return 1
    stage_mounted_in_any_namespace "$candidate" && return 1
    process_rooted_in_stage "$candidate" && return 1
    for backing in /sys/block/loop*/loop/backing_file; do
        [ -r "$backing" ] || continue
        target=$(cat "$backing" 2>/dev/null || :)
        case "$target" in *kindle-android/data.img*|*system-cache.img*)
            log "WATCHDOG_KEEP_STAGE reason=loop_still_attached backing=$target stage=$candidate"
            return 1;;
        esac
    done
    rm -rf "$candidate" || return 1
    [ ! -e "$candidate" ]
}

owner=$(lock_owner)
if [ -d "$LOCK" ] && [ "$owner" = "$LAUNCHER" ] && launcher_is_alive "$LAUNCHER"; then
    state=$(head -n1 "$BASE/warm-state" 2>/dev/null || :)
    case "$state" in
        native|native_pending)
            # A dead session controller cannot consume resume/exit commands.
            # Recover immediately instead of waiting forever for a launcher
            # that is itself blocked waiting for the frozen Android runner.
            if controller_is_alive; then
                while [ -d "$LOCK" ] && [ "$(lock_owner)" = "$LAUNCHER" ] && launcher_is_alive "$LAUNCHER"; do
                    sleep 1
                done
            else
                log "WATCHDOG_DEAD_CONTROLLER_RECOVERY launcher=$LAUNCHER state=$state"
            fi
            ;;
        *)
            while [ -d "$LOCK" ] && [ "$(lock_owner)" = "$LAUNCHER" ] && launcher_is_alive "$LAUNCHER"; do
                sleep 1
            done
            ;;
    esac
fi
[ -e "$MARKER" ] || [ -e "$BASE/mtp-held" ] || [ -e "$BASE/warm-freezer.stage" ] || exit 0

owner=$(lock_owner)
log "WATCHDOG_ORPHAN_DETECTED launcher=$LAUNCHER owner=$(lock_owner)"

# Serialize competing recovery helpers, but only after the healthy-controller
# wait above. This lets the entry point join an already-running repair without
# blocking the original watchdog that normally waits for clean shutdown.
RECOVERY_LOCK=$BASE/orphan-recovery.lock
if ! mkdir "$RECOVERY_LOCK" 2>/dev/null; then
    recovery_pid=$(cat "$RECOVERY_LOCK/pid" 2>/dev/null || :)
    case "$recovery_pid" in ''|*[!0-9]*) recovery_pid=;; esac
    if [ -n "$recovery_pid" ] && kill -0 "$recovery_pid" 2>/dev/null; then
        log "WATCHDOG_RECOVERY_ALREADY_RUNNING pid=$recovery_pid"
        exit 0
    fi
    rm -rf "$RECOVERY_LOCK"
    mkdir "$RECOVERY_LOCK" 2>/dev/null || exit 0
fi
echo "$$" > "$RECOVERY_LOCK/pid"
trap 'rm -rf "$RECOVERY_LOCK"' EXIT HUP INT TERM

stage=
if read_valid_stage; then
    log "WATCHDOG_STAGE_FOUND stage=$stage"
    if [ -f "$BASE/warm-native" ];then
        : > "$MARKER"
        /sbin/stop lab126_gui >> "$LOG" 2>&1 || :
        /sbin/stop x >> "$LOG" 2>&1 || :
    fi
    [ ! -x "$BASE/session-freezer" ] || "$BASE/session-freezer" thaw "$stage" >> "$LOG" 2>&1 || :
    request_android_exit "$stage" || :
    n=0
    while [ "$n" -lt 90 ] && { process_rooted_in_stage "$stage" || stage_mounted_in_any_namespace "$stage"; }; do
        request_android_exit "$stage" || :
        sleep 1
        n=$((n + 1))
    done
    if process_rooted_in_stage "$stage" || stage_mounted_in_any_namespace "$stage"; then
        log "WATCHDOG_RECOVERY_DEFERRED reason=container_still_active stage=$stage"
        exit 3
    fi
    if [ -f "$stage/run.sh" ]; then
        log "WATCHDOG_CONTAINER_CLEANUP_BEGIN stage=$stage"
        if sh "$stage/run.sh" "$stage" --cleanup-only >> "$LOG" 2>&1; then
            log "WATCHDOG_CONTAINER_CLEANUP_RC=0"
        else
            rc=$?
            log "WATCHDOG_CONTAINER_CLEANUP_RC=$rc stage=$stage"
        fi
    fi
    if [ -f "$stage/overlay-cleanup.sh" ]; then
        # Generated by start.sh from the hash-verified, path-checked overlay.
        ( stage=$stage; . "$stage/overlay-cleanup.sh" ) >> "$LOG" 2>&1 || log "WATCHDOG_OVERLAY_CLEANUP_FAILED=1"
    fi
    if [ -d "$stage" ] && [ ! -e "$stage/hwtcon-backup-ready" ] &&
       ! stage_mounted_in_any_namespace "$stage" && ! process_rooted_in_stage "$stage"; then
        find "$stage" -depth -type d -exec rmdir {} \; 2>/dev/null || :
    fi
    if [ -e "$stage" ]; then
        bytes=$(du -sk "$stage" 2>/dev/null | awk '{print $1}' || echo unknown)
        log "WATCHDOG_STAGE_PRESERVED bytes_kib=$bytes stage=$stage"
    else
        log "WATCHDOG_STAGE_REMOVED=1 stage=$stage"
    fi
else
    log 'WATCHDOG_NO_VALID_STAGE=1'
fi

[ ! -x "$BASE/cleanup-project-loops.sh" ] || "$BASE/cleanup-project-loops.sh" >> "$LOG" 2>&1 || log 'WATCHDOG_LOOP_CLEANUP_FAILED=1'
[ -z "$stage" ] || [ ! -x "$BASE/session-freezer" ] || "$BASE/session-freezer" release "$stage" >> "$LOG" 2>&1 || log 'WATCHDOG_FREEZER_RELEASE_FAILED=1'
[ ! -x "$BASE/session-power-guard" ] || "$BASE/session-power-guard" --restore >> "$LOG" 2>&1 || :
if sh "$BASE/restore-framework.sh" >> "$LOG" 2>&1; then
    log 'WATCHDOG_FRAMEWORK_RESTORED=1'
else
    log 'WATCHDOG_FRAMEWORK_RESTORE_FAILED=1'
    exit 4
fi
[ ! -x "$BASE/activate-native-home.sh" ] || {
    if sh "$BASE/activate-native-home.sh" --wait >> "$LOG" 2>&1; then
        log 'WATCHDOG_NATIVE_HOME_CONFIRMED=1'
    else
        log 'WATCHDOG_NATIVE_HOME_CONFIRM_FAILED=1'
    fi
}
[ ! -x "$BASE/storage-gate.sh" ] || sh "$BASE/storage-gate.sh" release >> "$LOG" 2>&1 || log 'WATCHDOG_MTP_RESTORE_DEFERRED=1'
rm -f "$BASE/warm-native" "$BASE/warm-state" "$BASE/warm-controller.pid" "$BASE/warm.command"

owner=$(lock_owner)
if [ -d "$LOCK" ] && ! launcher_is_alive "$owner"; then
    rm -rf "$LOCK"
    log 'WATCHDOG_STALE_LOCK_REMOVED=1'
fi
host_free=$(df -Pk /mnt/us 2>/dev/null | tail -n1 | awk '{print $4}' || echo unknown)
log "WATCHDOG_RECOVERY_COMPLETE free_kib=$host_free"
