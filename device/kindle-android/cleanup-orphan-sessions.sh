#!/bin/sh
set -u

BASE=/mnt/us/kindle-android
LOG=$BASE/session-cleanup.log
LOCK=$BASE/session.lock

log() { printf '%s\n' "$*" >> "$LOG"; }
free_kib() { df -Pk /mnt/us | tail -n 1 | awk '{print $4}'; }
mounted_below() {
    candidate=$1
    for info in /proc/[0-9]*/mountinfo; do
        grep -Fq "$candidate" "$info" 2>/dev/null && return 0
    done
    return 1
}
referenced_by_process() {
    candidate=$1
    for proc in /proc/[0-9]*; do
        [ -d "$proc" ] || continue
        root=$(readlink "$proc/root" 2>/dev/null || :)
        case "$root" in "$candidate"|"$candidate"/*) return 0;; esac
        cwd=$(readlink "$proc/cwd" 2>/dev/null || :)
        case "$cwd" in "$candidate"|"$candidate"/*) return 0;; esac
        cmd=$(tr '\000' ' ' < "$proc/cmdline" 2>/dev/null || :)
        case "$cmd" in *"$candidate"*) return 0;; esac
        for fd in "$proc"/fd/*; do
            target=$(readlink "$fd" 2>/dev/null || :)
            case "$target" in
                "$candidate"|"$candidate"/*|"$candidate"\ \(deleted\)|"$candidate"/*\ \(deleted\)) return 0;;
            esac
        done
    done
    for backing in /sys/block/loop*/loop/backing_file; do
        [ -r "$backing" ] || continue
        target=$(cat "$backing" 2>/dev/null || :)
        case "$target" in "$candidate"|"$candidate"/*) return 0;; esac
    done
    return 1
}

if [ ! -r "$LOCK/pid" ] || [ "$(cat "$LOCK/pid" 2>/dev/null || :)" != "${KINDLE_ANDROID_LAUNCHER_PID:-}" ] ||
   [ -z "${KINDLE_ANDROID_LAUNCHER_PID:-}" ]; then
    log 'SESSION_RECLAIM_ABORT reason=launcher_lock_not_owned'
    exit 1
fi
if [ -e "$BASE/framework-stopped" ]; then
    log 'SESSION_RECLAIM_ABORT reason=framework_stopped'
    exit 1
fi

before=$(free_kib)
log "SESSION_RECLAIM_BEGIN host_free_kib=$before"
for candidate in /mnt/us/kindle-a7-art.* "$BASE"/exited-session-*; do
    [ -d "$candidate" ] || continue
    [ ! -L "$candidate" ] || { log "SESSION_RECLAIM_SKIP path=$candidate reason=symlink"; continue; }
    case "$candidate" in
        /mnt/us/kindle-a7-art.*|"$BASE"/exited-session-*) ;;
        *) log "SESSION_RECLAIM_SKIP path=$candidate reason=path_outside_allowlist"; continue;;
    esac
    resolved=$(readlink -f "$candidate" 2>/dev/null || :)
    [ "$resolved" = "$candidate" ] || { log "SESSION_RECLAIM_SKIP path=$candidate reason=canonical_path_mismatch"; continue; }
    if mounted_below "$candidate"; then
        log "SESSION_RECLAIM_SKIP path=$candidate reason=mount_present_in_namespace"
        continue
    fi
    if referenced_by_process "$candidate"; then
        log "SESSION_RECLAIM_SKIP path=$candidate reason=process_or_fd_reference"
        continue
    fi
    completed=0
    if [ -f "$candidate/exit-code.txt" ] && [ "$(cat "$candidate/exit-code.txt" 2>/dev/null || :)" = 0 ] &&
       grep -q 'APK_PROBE_COMPLETE=1' "$candidate/result.txt" 2>/dev/null; then
        completed=1
        reclaim_reason=completed_session
    elif [ ! -e "$candidate/rootfs" ] && [ ! -e "$candidate/run.sh" ] &&
         [ ! -e "$candidate/hwtcon-backup-ready" ] &&
         [ "$(find "$candidate" -type f 2>/dev/null | wc -l | tr -d ' ')" = 1 ] &&
         [ "$(find "$candidate" -type d 2>/dev/null | wc -l | tr -d ' ')" = 1 ] &&
         [ -f "$candidate/overlay-cleanup.sh" ]; then
        completed=1
        reclaim_reason=abandoned_overlay_bootstrap
    elif [ -f "$candidate/run.sh" ] && [ -d "$candidate/rootfs" ]; then
        completed=1
        reclaim_reason=orphan_session_cleanup
    fi
    [ "$completed" = 1 ] || {
        log "SESSION_RECLAIM_SKIP path=$candidate reason=completion_marker_missing"
        continue
    }
    if [ "$reclaim_reason" = orphan_session_cleanup ]; then
        # A device number left in an unmounted orphan is not proof of ownership:
        # the kernel can reuse loopN for another session or a native image.
        # cleanup-project-loops.sh owns the checked resource reclamation; never
        # let this old generated run.sh detach a device from a stale record.
        for loop_record in system-loop-device data-loop-device; do
            if [ -f "$candidate/$loop_record" ]; then
                retired_loop="$BASE/retired-${candidate##*/}-$loop_record"
                [ ! -e "$retired_loop" ] || { log "SESSION_RECLAIM_SKIP path=$candidate reason=retired_loop_record_exists"; continue 2; }
                mv "$candidate/$loop_record" "$retired_loop" || exit 1
                log "SESSION_ORPHAN_LOOP_RECORD_RETIRED path=$candidate record=$loop_record"
            fi
        done
        if [ -f "$candidate/hwtcon-backup-ready" ] &&
           [ ! -e /mnt/us/.kindle-a7-hwtcon-restore.bin ] &&
           [ ! -L /mnt/us/.kindle-a7-hwtcon-restore.bin ]; then
            gui_state=$(/sbin/initctl status lab126_gui 2>&1 || :)
            x_state=$(/sbin/initctl status x 2>&1 || :)
            case "$gui_state:$x_state" in
                *'start/running'*'start/running'*)
                    # Mount/process/fd checks above established the stage is
                    # orphaned. No snapshot exists to restore, and native GUI
                    # already owns the screen. Preserve evidence, retire only
                    # the empty pending marker rather than replaying old pixels.
                    retired="$BASE/retired-screen-marker-${candidate##*/}"
                    if [ -e "$retired" ]; then
                        log "SESSION_RECLAIM_SKIP path=$candidate reason=retired_marker_exists"
                        continue
                    fi
                    mv "$candidate/hwtcon-backup-ready" "$retired" || exit 1
                    log "SESSION_ORPHAN_SCREEN_MARKER_RETIRED path=$candidate reason=no_snapshot_native_gui_running"
                    ;;
                *) log "SESSION_RECLAIM_SKIP path=$candidate reason=native_gui_not_ready"; continue;;
            esac
        fi
        if [ -f "$candidate/result.txt" ]; then
            recovery_log="$BASE/session-recovery-$(date +%Y%m%d-%H%M%S).log"
            cp -p "$candidate/result.txt" "$recovery_log" 2>/dev/null || :
        fi
        log "SESSION_ORPHAN_CLEANUP_BEGIN path=$candidate"
        if ! sh "$candidate/run.sh" "$candidate" --cleanup-only >> "$LOG" 2>&1; then
            log "SESSION_RECLAIM_SKIP path=$candidate reason=container_cleanup_failed"
            continue
        fi
        if [ -f "$candidate/overlay-cleanup.sh" ]; then
            ( stage=$candidate; . "$candidate/overlay-cleanup.sh" ) >> "$LOG" 2>&1 || {
                log "SESSION_RECLAIM_SKIP path=$candidate reason=overlay_cleanup_failed"
                continue
            }
        fi
        [ ! -e "$candidate/hwtcon-backup-ready" ] || { log "SESSION_RECLAIM_SKIP path=$candidate reason=display_recovery_pending"; continue; }
        mounted_below "$candidate" && { log "SESSION_RECLAIM_SKIP path=$candidate reason=mount_present_after_cleanup"; continue; }
        referenced_by_process "$candidate" && { log "SESSION_RECLAIM_SKIP path=$candidate reason=process_reference_after_cleanup"; continue; }
        rm -f "$candidate/result.txt" "$candidate/exit-code.txt"
        find "$candidate" -depth -type d -exec rmdir {} \; 2>/dev/null || :
        [ ! -e "$candidate" ] || { log "SESSION_RECLAIM_SKIP path=$candidate reason=cleanup_left_files"; continue; }
        log "SESSION_ORPHAN_CLEANUP_DONE path=$candidate"
        continue
    fi
    bytes=$(du -sk "$candidate" 2>/dev/null | awk '{print $1}')
    case "$bytes" in ''|*[!0-9]*) log "SESSION_RECLAIM_SKIP path=$candidate reason=size_unavailable"; continue;; esac
    log "SESSION_RECLAIM_REMOVE_BEGIN path=$candidate bytes_kib=$bytes reason=$reclaim_reason"
    rm -rf "$candidate" || { log "SESSION_RECLAIM_FAILED path=$candidate reason=remove_error"; continue; }
    [ ! -e "$candidate" ] || { log "SESSION_RECLAIM_FAILED path=$candidate reason=still_exists"; continue; }
    log "SESSION_RECLAIM_REMOVED path=$candidate bytes_kib=$bytes"
done
after=$(free_kib)
log "SESSION_RECLAIM_END host_free_kib_before=$before host_free_kib_after=$after"
exit 0
