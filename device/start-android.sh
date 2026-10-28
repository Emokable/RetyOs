#!/bin/sh
# Name: 启动 RetyOs
# Author: Kindle Android DIY
# DontUseFBInk
# Kindle Android session launcher. Intended for SH_Integration / Scriptlet.
# Android runs only in a temporary userspace; Kindle system files are untouched.

ENTRY_MARKER='KINDLE_ANDROID_DOCUMENT_ENTRY=1'
U=/mnt/us
[ -d /mnt/base-us/documents ] && U=/mnt/base-us
BASE=/mnt/us/kindle-android
OUT="$BASE/entry.log"
entry_resumed=0

run_entry() {
    echo "$ENTRY_MARKER"
    [ ! -e "$BASE/release-maintenance.lock" ] || { echo ANDROID_ENTRY_MAINTENANCE=1;return 16; }
    if [ -x "$BASE/session-freezer" ];then
        "$BASE/session-freezer" now > "$BASE/entry-request.started-ns"
    fi
    date
    if [ ! -x "$BASE/start.sh" ] || [ ! -r "$BASE/payload.tar.gz" ]; then
        echo "ANDROID_ENTRY_ERROR=container_files_missing"
        return 2
    fi
    # A one-shot preflight covers both cold entry and warm resume. There is no
    # polling guard during Android runtime/retention; MTP stays stopped by the
    # storage gate until full container teardown.
    if [ -d "$BASE/session.lock" ]; then
        pid=$(cat "$BASE/session.lock/pid" 2>/dev/null || :)
        case "$pid" in ''|*[!0-9]*) :;; *)
            if kill -0 "$pid" 2>/dev/null; then
                warm_state=$(head -n1 "$BASE/warm-state" 2>/dev/null || :)
                if [ "$warm_state" = native ] || [ "$warm_state" = native_pending ]; then
                    # Frozen-retained Android may expose Kindle MTP through
                    # protected image aliases. Resume itself checks that the
                    # USB host has disconnected before closing that export.
                    controller=$(cat "$BASE/warm-controller.pid" 2>/dev/null || :)
                    case "$controller" in ''|*[!0-9]*) echo ANDROID_ENTRY_ERROR=warm_controller_missing;return 4;; esac
                    kill -0 "$controller" 2>/dev/null || { echo ANDROID_ENTRY_ERROR=warm_controller_dead;return 4; }
                    printf 'resume\n' > "$BASE/warm.command.new"
                    mv "$BASE/warm.command.new" "$BASE/warm.command"
                    entry_resumed=1
                    echo ANDROID_ENTRY_WARM_RESUME_REQUESTED=1
                    return 0
                fi
                echo "ANDROID_ENTRY_ALREADY_RUNNING=1 pid=$pid"
                return 0
            fi;;
        esac
    fi
    if [ -x "$BASE/usb-session-guard" ] && ! "$BASE/usb-session-guard" --check; then
        echo "ANDROID_ENTRY_ERROR=usb_data_host_connected_disconnect_before_entry"
        return 15
    fi
    for stage in /mnt/us/kindle-a7-art.*; do
        [ -d "$stage" ] || continue
        if [ -f "$stage/hwtcon-backup-ready" ] && [ -e /mnt/us/.kindle-a7-hwtcon-restore.bin ]; then
            echo "ANDROID_ENTRY_REFUSED=recovery_pending stage=$stage"
            return 3
        fi
        if [ -f "$stage/hwtcon-backup-ready" ]; then
            # The lock-owning launcher checks mounts/processes and a healthy
            # native GUI before retiring orphaned markers with no snapshot.
            echo "ANDROID_ENTRY_DEFER_ORPHAN_RECOVERY_CHECK stage=$stage"
        fi
    done
    : > "$BASE/session.log"
    if command -v setsid >/dev/null 2>&1; then
        nohup setsid /bin/sh "$BASE/start.sh" >> "$BASE/session.log" 2>&1 < /dev/null &
    else
        nohup /bin/sh "$BASE/start.sh" >> "$BASE/session.log" 2>&1 < /dev/null &
    fi
    launcher_pid=$!
    echo "$launcher_pid" > "$BASE/launcher.pid"
    echo "ANDROID_ENTRY_START_REQUESTED=1 pid=$launcher_pid expected_wait=2-3_minutes"
}
run_entry > "$OUT" 2>&1
rc=$?
if [ "$rc" -eq 0 ]; then
    message='Starting RetyOs...'
else
    message='RetyOs could not start. See kindle-android/entry.log'
fi
if [ "$entry_resumed" = 1 ]; then
    : # The retained compositor owns the next frame; avoid drawing over it.
elif [ -x /var/local/kmc/bin/fbink ]; then
    /var/local/kmc/bin/fbink -q -pm -y 12 "$message" >/dev/null 2>&1 || :
    sleep 3
elif command -v eips >/dev/null 2>&1; then
    eips 2 20 "$message" >/dev/null 2>&1 || :
fi
echo "$OUT"
exit "$rc"
