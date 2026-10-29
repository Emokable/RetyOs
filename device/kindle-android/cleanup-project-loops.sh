#!/bin/sh
# Reclaim only unused container image bindings; native Kindle loops are excluded.
set -u
BASE=/mnt/us/kindle-android
log() { echo "$*"; echo "$*" >> "$BASE/session-cleanup.log"; }
if [ -r "$BASE/session.lock/pid" ]; then
    owner=$(cat "$BASE/session.lock/pid" 2>/dev/null || :)
    if [ "$owner" != "${KINDLE_ANDROID_LAUNCHER_PID:-}" ] && kill -0 "$owner" 2>/dev/null; then
        log PROJECT_LOOP_CLEANUP_DEFERRED=live_launcher
        exit 0
    fi
fi
# With no matching backing image there is nothing to detach. Avoid a full
# process/mount/fd audit on every clean cold start; retain it for real bindings.
project_binding=0
for backing_file in /sys/block/loop*/loop/backing_file; do
    [ -r "$backing_file" ] || continue
    case "$(cat "$backing_file")" in
        /mnt/base-us/kindle-android/system-cache.img|/mnt/us/kindle-android/system-cache.img|/kindle-android/system-cache.img|/mnt/base-us/kindle-android/data.img|/mnt/us/kindle-android/data.img|/kindle-android/data.img)
            project_binding=1;break;;
    esac
done
if [ "$project_binding" = 0 ]; then
    log PROJECT_LOOP_CLEANUP_SKIPPED=no_project_bindings
    exit 0
fi
for proc in /proc/[0-9]*; do
    comm=$(cat "$proc/comm" 2>/dev/null || :)
    [ "$comm" != art-guard ] || { log PROJECT_LOOP_CLEANUP_DEFERRED=live_android; exit 0; }
done
for backing_file in /sys/block/loop*/loop/backing_file; do
    [ -r "$backing_file" ] || continue
    backing=$(cat "$backing_file" 2>/dev/null || :)
    case "$backing" in
        /mnt/base-us/kindle-android/system-cache.img|/mnt/us/kindle-android/system-cache.img|/kindle-android/system-cache.img) image=$BASE/system-cache.img;;
        /mnt/base-us/kindle-android/data.img|/mnt/us/kindle-android/data.img|/kindle-android/data.img) image=$BASE/data.img;;
        *) continue;;
    esac
    device=${backing_file#/sys/block/}; device=${device%%/*}
    number=$(cat "/sys/block/$device/dev")
    busy=0
    for info in /proc/[0-9]*/mountinfo; do
        if awk -v d="$number" '$3==d {found=1} END {exit !found}' "$info" 2>/dev/null; then busy=1; break; fi
    done
    if [ "$busy" = 0 ]; then
        for fd in /proc/[0-9]*/fd/*; do
            target=$(readlink "$fd" 2>/dev/null || :)
            case "$target" in "/dev/$device"|"$image"|"$backing"|"/mnt/base-us/kindle-android/${image##*/}") busy=1; break;; esac
        done
    fi
    if [ "$busy" = 1 ]; then log "PROJECT_LOOP_RETAINED=$device reason=mounted_or_open"; continue; fi
    # Recheck the exact backing immediately before the reversible detach.
    [ "$(cat "$backing_file" 2>/dev/null || :)" = "$backing" ] || continue
    if losetup -d "/dev/$device"; then log "PROJECT_LOOP_RELEASED=$device image=$image";
    else log "PROJECT_LOOP_RELEASE_FAILED=$device"; exit 1; fi
done
