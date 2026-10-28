#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
if [ -r "$BASE/session.lock/pid" ]; then
    pid=$(cat "$BASE/session.lock/pid" 2>/dev/null || :)
    case "$pid" in ''|*[!0-9]*) :;; *)
        if kill -0 "$pid" 2>/dev/null && [ -r "/proc/$pid/cmdline" ]; then
            command=$(tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null || :)
            case "$command" in
                *'/mnt/us/kindle-android/start.sh'*)
                    echo ANDROID_RECOVERY_REFUSED=session_still_running
                    exit 1;;
            esac
        fi
        ;;
    esac
fi
found=0
for stage in /mnt/us/kindle-a7-art.*; do
    [ -d "$stage" ] || continue
    [ -f "$stage/hwtcon-backup-ready" ] || continue
    found=1
    echo "ANDROID_RECOVERY_STAGE=$stage"
    sh "$stage/run.sh" "$stage" --cleanup-only
    [ ! -d "$stage" ] || { echo "ANDROID_RECOVERY_INCOMPLETE=1 stage=$stage"; exit 2; }
done
[ "$found" = 1 ] || echo ANDROID_RECOVERY_NOT_NEEDED=1
sh "$BASE/restore-framework.sh"
if [ "${1:-}" = --no-home ]; then exit 0; fi
sh "$BASE/activate-native-home.sh" --wait
