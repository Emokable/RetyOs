#!/bin/sh
set -eu

BASE=/mnt/us/kindle-android
export KINDLE_LOOP_DEVICE_HELPER=$BASE/kernel-device-nodes.sh
LOCK=$BASE/session.lock
PAYLOAD=$BASE/payload.tar.gz
DATA_IMAGE=$BASE/data.img
DATA_IMAGE_BYTES=1610612736
MARKER=$BASE/framework-stopped
WATCHDOG=$BASE/framework-watchdog.log
framework_stopped=0
network_relay_pid=
power_guard_pid=
progress_pid=
warm_session_pid=
usb_guard_pid=
owns_lock=0
home_launch_pending=0
extra_overlay_files=
stage=
cleanup_status=not_started

# --- startup tuning: sub-second service-stop polling ------------------------
# lab126_gui and X normally reach 'stop/waiting' in well under a second, but a
# 1 s tick costs a full second before the first check and rounds every stop up
# to the next whole second. Poll every 200 ms instead and keep the same 30 s
# budget (150 ticks). usleep is missing on some firmwares, so fall back to the
# original 1 s tick; the budget stays 30 s either way.
POLL_UNIT_MS=1000
if usleep 1000 2>/dev/null; then POLL_UNIT_MS=200; fi
POLL_LIMIT=$(( 30000 / POLL_UNIT_MS ))
poll_tick() {
    if [ "$POLL_UNIT_MS" = 200 ]; then usleep 200000 || sleep 1; else sleep 1; fi
}

# --- startup tuning: sha256 result cache ------------------------------------
# Every cold boot re-hashes the bootstrap tarballs (tens of MB on the FAT32
# user partition) only to compare them against a digest recorded at build time.
# Hashing is only required when the file may have changed, so remember the
# size:mtime signature next to the digest and skip the re-hash while both are
# unchanged. Any rewrite of the file updates mtime, which invalidates the entry;
# a cache miss simply falls back to hashing.
digest_cache() {  # $1=file $2=cache file -> prints sha256 of $1
    _dc_sig=$(stat -c '%s:%Y' "$1" 2>/dev/null || :)
    if [ -n "$_dc_sig" ] && [ -f "$2" ] && [ ! -L "$2" ]; then
        if [ "$(sed -n '1p' "$2" 2>/dev/null || :)" = "$_dc_sig" ]; then
            _dc_sum=$(sed -n '2p' "$2" 2>/dev/null || :)
            case "$_dc_sum" in
                ''|*[!0-9a-f]*) : ;;
                *) if [ "${#_dc_sum}" -eq 64 ]; then printf '%s\n' "$_dc_sum"; return 0; fi ;;
            esac
        fi
    fi
    _dc_sum=$(sha256sum "$1" | awk '{print $1}')
    if [ -n "$_dc_sig" ]; then
        if printf '%s\n%s\n' "$_dc_sig" "$_dc_sum" > "$2.tmp.$$" 2>/dev/null; then
            mv "$2.tmp.$$" "$2" 2>/dev/null || rm -f "$2.tmp.$$" 2>/dev/null || :
        else
            rm -f "$2.tmp.$$" 2>/dev/null || :
        fi
    fi
    printf '%s\n' "$_dc_sum"
}

# BOOT_PROGRESS_UI=1. Stage-based progress, not an estimate of bytes copied.
# Clear once with GC16; all subsequent progress draws use partial GL16.
progress_status() {
    printf '%s\n%s\n' "$1" "$2" > "$BASE/boot-progress.state.new"
    mv "$BASE/boot-progress.state.new" "$BASE/boot-progress.state"
    echo "ANDROID_BOOT_STAGE=$1 label=$2 monotonic=$(cut -d ' ' -f1 /proc/uptime)"
}
stop_progress() {
    if [ -n "$progress_pid" ]; then
        kill "$progress_pid" 2>/dev/null || :
        wait "$progress_pid" 2>/dev/null || :
        progress_pid=
    fi
}
start_progress() {
    : > "$BASE/boot-progress.log"
    rm -f "$BASE/boot-progress.ready"
    rm -f "$BASE/boot-progress.stage"
    (
        trap - EXIT HUP INT TERM
        trap 'exit 0' TERM INT
        started=$(date +%s)
        last_draw=0
        last_percent=
        first=1
        initial_frame_request_sent=0
        final_frame_request_sent=0
        touch_grab_warning_logged=0
        # Native display writers have been released or held before this worker.
        # Render branding once; subsequent updates touch only the progress area.
        sh "$BASE/boot-progress-ui.sh" init || :
        echo BOOT_PROGRESS_INITIAL_FULL_CLEAR=1
        while kill -0 "$$" 2>/dev/null && [ -d "$LOCK" ]; do
            percent=$(head -n 1 "$BASE/boot-progress.state" 2>/dev/null || echo 5)
            label=$(sed -n '2p' "$BASE/boot-progress.state" 2>/dev/null || :)
            current_stage=$(cat "$BASE/boot-progress.stage" 2>/dev/null || :)
            result="$current_stage/result.txt"
            if [ -r "$result" ]; then
                if grep -q 'APK_ACTIVITY_PROCESS_LAUNCH_PASS=1' "$result"; then
                    percent=96; label='正在确认显示与触控'
                    guard=$("$BASE/session-freezer" guard "$current_stage" 2>/dev/null || :)
                    tmp="/proc/$guard/root/data/local/tmp"
                    if [ -n "$guard" ] && [ "$initial_frame_request_sent" = 0 ] && [ ! -f "$tmp/kindle-display-initial-frame-complete" ]; then
                        rm -f "$tmp/kindle-display-initial-frame-complete" "$tmp/kindle-display-initial-frame-request"
                        : > "$tmp/kindle-display-full-refresh"
                        : > "$tmp/kindle-display-initial-frame-request"
                        initial_frame_request_sent=1
                        echo "BOOT_PROGRESS_INITIAL_FRAME_REQUESTED=1 elapsed=$(($(date +%s)-started))"
                    fi
                    if [ -n "$guard" ] && [ -f "$tmp/kindle-display-initial-frame-complete" ]; then
                        if [ "$touch_grab_warning_logged" = 0 ] && ! grep -q 'KINDLE_TOUCH_EXCLUSIVE_GRAB=1' "$result"; then
                            echo "BOOT_PROGRESS_TOUCH_GRAB_MARKER_MISSING=1 elapsed=$(($(date +%s)-started))"
                            touch_grab_warning_logged=1
                        fi
                        if [ "$final_frame_request_sent" = 0 ]; then
                            rm -f "$tmp/kindle-display-resumed"
                            : > "$tmp/kindle-display-full-refresh"
                            : > "$tmp/kindle-display-resume"
                            final_frame_request_sent=1
                            echo "BOOT_PROGRESS_FINAL_REFRESH_REQUESTED=1 elapsed=$(($(date +%s)-started))"
                        elif [ -f "$tmp/kindle-display-resumed" ]; then
                            percent=100; label='RetyOs 已就绪'
                        fi
                    fi
                elif grep -q 'APK_PM_INSTALL_PASS=1' "$result"; then
                    percent=92; label='正在打开桌面'
                elif grep -q 'APK_PM_INSTALL_BEGIN=1' "$result"; then
                    percent=88; label='正在准备桌面'
                elif grep -q 'APK_FRAMEWORK_SERVICES_READY' "$result"; then
                    percent=82; label='正在连接系统服务'
                elif grep -q 'APK_PROBE_RUNNER_STARTED\|ZYGOTE_USERNS_READY' "$result"; then
                    percent=72; label='正在启动系统服务'
                elif grep -q 'ANDROID_DATA_IMAGE_MOUNTED=1' "$result"; then
                    percent=62; label='正在载入应用数据'
                fi
            fi
            now=$(date +%s)
            elapsed=$((now - started))
            if [ "$percent" = 100 ]; then
                progress_status 100 'RetyOs 已就绪'
                echo "BOOT_PROGRESS_READY=1 elapsed=$elapsed"
                ready_ns=$("$BASE/session-freezer" now)
                requested_ns=$(head -n1 "$BASE/cold-entry.started-ns" 2>/dev/null || echo 0)
                case "$requested_ns" in ''|*[!0-9]*) requested_ns=$ready_ns;;esac
                if [ "$requested_ns" -le 0 ] || [ "$requested_ns" -gt "$ready_ns" ];then requested_ns=$ready_ns;fi
                echo "ANDROID_COLD_SOFTWARE_READY monotonic_ns=$ready_ns requested_ns=$requested_ns duration_ms=$(((ready_ns-requested_ns)/1000000))"
                break
            fi
            if [ "$percent" != "$last_percent" ] || [ $((now - last_draw)) -ge 8 ]; then
                sh "$BASE/boot-progress-ui.sh" progress "$percent" "$label" "$elapsed" || :
                echo "BOOT_PROGRESS_DRAW percent=$percent elapsed=$elapsed label=$label"
                if [ "$first" = 1 ]; then : > "$BASE/boot-progress.ready"; fi
                first=0; last_draw=$now; last_percent=$percent
            fi
            sleep 1
        done
    ) >> "$BASE/boot-progress.log" 2>&1 &
    progress_pid=$!
    # Do not let system preparation race ahead and overwrite the initial
    # progress screen before it has actually reached the e-ink display.
    wait_i=0
    while [ ! -f "$BASE/boot-progress.ready" ] && [ "$wait_i" -lt 10 ] && kill -0 "$progress_pid" 2>/dev/null; do
        wait_i=$((wait_i + 1)); sleep 1
    done
    if [ -f "$BASE/boot-progress.ready" ]; then echo BOOT_PROGRESS_INITIAL_STAGE_VISIBLE=1; else echo BOOT_PROGRESS_INITIAL_STAGE_VISIBLE=0; fi
}

release_lock() {
    rm -f "$LOCK/pid"
    rmdir "$LOCK" 2>/dev/null || :
}
finish() {
    rc=$?
    trap - EXIT
    stop_progress
    if [ -n "$warm_session_pid" ]; then
        kill "$warm_session_pid" 2>/dev/null || :
        wait "$warm_session_pid" 2>/dev/null || :
        warm_session_pid=
    fi
    if [ -n "$stage" ] && [ -x "$BASE/session-freezer" ]; then "$BASE/session-freezer" thaw "$stage" || :; fi
    if [ -n "$power_guard_pid" ]; then
        kill "$power_guard_pid" 2>/dev/null || :
        wait "$power_guard_pid" 2>/dev/null || :
        power_guard_pid=
    fi
    if [ -n "$network_relay_pid" ]; then
        kill "$network_relay_pid" 2>/dev/null || :
        wait "$network_relay_pid" 2>/dev/null || :
        network_relay_pid=
    fi
    if [ -n "$usb_guard_pid" ]; then
        kill "$usb_guard_pid" 2>/dev/null || :
        wait "$usb_guard_pid" 2>/dev/null || :
        usb_guard_pid=
    fi
    [ ! -x "$BASE/session-power-guard" ] || "$BASE/session-power-guard" --restore >> "$BASE/session.log" 2>&1 || :
    if [ "$framework_stopped" = 1 ]; then
        if sh "$BASE/restore-framework.sh" >> "$BASE/session.log" 2>&1; then
            framework_stopped=0
        else
            echo ANDROID_FRAMEWORK_RECOVERY_REQUIRED=1 >> "$BASE/session.log"
            rc=7
        fi
    fi
    if [ "$owns_lock" = 1 ] && [ -x "$BASE/cleanup-project-loops.sh" ]; then
        KINDLE_ANDROID_LAUNCHER_PID=$$ "$BASE/cleanup-project-loops.sh" >> "$BASE/session.log" 2>&1 || rc=8
    fi
    if [ -n "$stage" ] && [ -x "$BASE/session-freezer" ]; then "$BASE/session-freezer" release "$stage" >> "$BASE/session.log" 2>&1 || rc=8; fi
    if [ "$owns_lock" = 1 ]; then
        rm -f "$BASE/warm-state" "$BASE/warm-native" "$BASE/warm.command" "$BASE/warm-controller.pid" "$BASE/warm-power.state" "$BASE/warm-controller.socket"
        [ ! -x "$BASE/storage-gate.sh" ] || sh "$BASE/storage-gate.sh" release >> "$BASE/session.log" 2>&1 || rc=8
    fi
    stage_bytes=0
    if [ -n "$stage" ] && [ -d "$stage" ]; then stage_bytes=$(du -sk "$stage" 2>/dev/null | awk '{print $1*1024}' || echo unknown); fi
    remaining_mounts=none
    if [ -n "$stage" ]; then remaining_mounts=$(awk -v p="$stage" '$2==p || index($2,p"/")==1 {printf "%s%s",sep,$2;sep=","}' /proc/mounts 2>/dev/null || :); fi
    host_free=$(df -Pk /mnt/us 2>/dev/null | tail -n 1 | awk '{print $4}' || echo unknown)
    printf "SESSION_EXIT_STATUS=%s\nSESSION_CLEANUP_STATUS=%s\nREMAINING_MOUNTS=%s\nTEMP_DIRECTORY_BYTES=%s\nHOST_FREE_SPACE_KIB=%s\n" "$rc" "$cleanup_status" "${remaining_mounts:-none}" "$stage_bytes" "$host_free" >> "$BASE/session.log"
    if [ "$owns_lock" = 1 ]; then release_lock; fi
    if [ "$home_launch_pending" = 1 ];then
        start_ns=$(head -n1 "$BASE/full-exit.started-ns" 2>/dev/null || echo 0)
        case "$start_ns" in ''|*[!0-9]*) start_ns=$($BASE/session-freezer now);;esac
        now_ns=$("$BASE/session-freezer" now)
        if [ "$start_ns" -le 0 ] || [ "$start_ns" -gt "$now_ns" ];then start_ns=$now_ns;fi
        printf '%s\n' "$start_ns" > "$BASE/native-home.trace-id.new"
        mv "$BASE/native-home.trace-id.new" "$BASE/native-home.trace-id"
        if sh "$BASE/activate-native-home.sh" --wait >> "$BASE/session.log" 2>&1;then
            end_ns=$("$BASE/session-freezer" now)
            case "$start_ns" in ''|*[!0-9]*) start_ns=$end_ns;;esac
            echo "FULL_EXIT_SOFTWARE_READY monotonic_ns=$end_ns duration_ms=$(((end_ns-start_ns)/1000000))" >> "$BASE/session.log"
        else rc=7;fi
    fi
    echo "SESSION_FINAL_STATUS=$rc monotonic_ns=$($BASE/session-freezer now)" >> "$BASE/session.log"
    exit "$rc"
}
trap finish EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

[ ! -e "$BASE/release-maintenance.lock" ] || { echo ANDROID_START_MAINTENANCE=1;exit 16; }
[ -f "$DATA_IMAGE" ] || { echo "ANDROID_START_ERROR=data_image_missing run=$BASE/prepare-data.sh"; exit 9; }
[ -x "$KINDLE_LOOP_DEVICE_HELPER" ] || { echo ANDROID_START_ERROR=kernel_device_helper_missing;exit 8; }
[ "$(stat -c '%s' "$DATA_IMAGE")" -eq "$DATA_IMAGE_BYTES" ] || { echo ANDROID_START_ERROR=data_image_size_invalid expected=$DATA_IMAGE_BYTES; exit 9; }
[ -r "$PAYLOAD" ] || { echo "ANDROID_START_ERROR=payload_missing path=$PAYLOAD"; exit 2; }
if [ -d "$LOCK" ]; then
    if [ -r "$LOCK/pid" ] && kill -0 "$(cat "$LOCK/pid")" 2>/dev/null; then echo ANDROID_START_ALREADY_RUNNING=1; exit 0; fi
    rm -f "$LOCK/pid"
    rmdir "$LOCK" 2>/dev/null || { echo ANDROID_START_ERROR=stale_lock_not_empty; exit 3; }
fi
mkdir "$LOCK" || { echo ANDROID_START_ALREADY_RUNNING=1; exit 0; }
owns_lock=1
echo $$ > "$LOCK/pid"
cp "$BASE/entry-request.started-ns" "$BASE/cold-entry.started-ns" 2>/dev/null || :
[ ! -e "$MARKER" ] || { echo ANDROID_START_ERROR=framework_recovery_pending; exit 10; }
if [ -x "$BASE/usb-session-guard" ] && ! "$BASE/usb-session-guard" --check; then
    echo ANDROID_START_ERROR=usb_host_connected_disconnect_usb_first
    exit 15
fi
if [ -x "$BASE/session-freezer" ] && [ -x "$BASE/warm-session.sh" ];then
    (cd "$BASE" && sha256sum -c session-freezer.sha256) || exit 8
    sh "$BASE/storage-gate.sh" hold || { echo ANDROID_START_ERROR=mtp_storage_gate_failed;exit 18; }
fi
# Keep a small, read-only USB state monitor alive for the whole Android
# session, including frozen retention. On host attach it asks ART to exit; the
# normal owner then detaches images and restores Kindle MTP in order.
if [ -x "$BASE/usb-session-guard" ]; then
    nohup "$BASE/usb-session-guard" "$$" >> "$BASE/usb-session-guard.log" 2>&1 < /dev/null &
    usb_guard_pid=$!
    echo "ANDROID_USB_SESSION_GUARD_STARTED=1 pid=$usb_guard_pid"
fi
if [ -x "$BASE/cleanup-project-loops.sh" ]; then
    KINDLE_ANDROID_LAUNCHER_PID=$$ "$BASE/cleanup-project-loops.sh" || { echo ANDROID_START_ERROR=loop_cleanup_failed; exit 16; }
fi
if [ -x "$BASE/cleanup-orphan-sessions.sh" ]; then
    KINDLE_ANDROID_LAUNCHER_PID=$$ "$BASE/cleanup-orphan-sessions.sh" || { echo ANDROID_START_ERROR=orphan_cleanup_guard_failed; exit 16; }
fi
rm -f "$BASE/warm-state" "$BASE/warm-native" "$BASE/warm.command" "$BASE/warm-controller.pid" "$BASE/warm-power.state" "$BASE/warm-controller.socket"
# A failed cold boot has no full-exit request. Do not reuse a prior session's
# request clock or native-ready receipt when measuring its recovery.
rm -f "$BASE/full-exit.started-ns" "$BASE/native-home.trace-id" "$BASE/native-ui.ready"
# Keep one additional compressed-memory swap device available for Android.
# This only adjusts runtime zram (never a file on /mnt/us or the system image).
# hot_add is read-to-create on this kernel, so read it only when zram1 is absent.
setup_android_zram_swap() {
    zram_target_bytes=268435456
    zram_dev=/dev/zram1
    zram_size_path=/sys/block/zram1/disksize
    if [ ! -b "$zram_dev" ] && [ -r /sys/block/zram1/dev ];then
        "$KINDLE_LOOP_DEVICE_HELPER" zram "$zram_dev" >/dev/null || {
            echo ANDROID_ZRAM_SWAP=unavailable reason=device_node_validation;return 0;
        }
    fi
    if grep -q "^$zram_dev[[:space:]]" /proc/swaps; then
        zram_active_size=$(cat "$zram_size_path" 2>/dev/null || echo unknown)
        echo "ANDROID_ZRAM_SWAP=already_active device=zram1 size_bytes=$zram_active_size"
        return 0
    fi
    if [ ! -b "$zram_dev" ]; then
        zram_hot_add=/sys/class/zram-control/hot_add
        if [ -r "$zram_hot_add" ]; then
            zram_new_id=$(cat "$zram_hot_add" 2>/dev/null || :)
            case "$zram_new_id" in ''|*[!0-9]*) echo ANDROID_ZRAM_SWAP=unavailable reason=hot_add_failed; return 0;; esac
            zram_dev=/dev/zram$zram_new_id
            zram_size_path=/sys/block/zram$zram_new_id/disksize
        else
            echo ANDROID_ZRAM_SWAP=unavailable reason=device_missing
            return 0
        fi
    fi
    if [ ! -b "$zram_dev" ];then
        "$KINDLE_LOOP_DEVICE_HELPER" zram "$zram_dev" >/dev/null || {
            echo ANDROID_ZRAM_SWAP=unavailable reason=block_device_missing;return 0;
        }
    fi
    if grep -q "^[^ ]* $zram_dev " /proc/mounts; then
        echo "ANDROID_ZRAM_SWAP=skipped reason=device_mounted device=$zram_dev"
        return 0
    fi
    zram_current_size=$(cat "$zram_size_path" 2>/dev/null || echo invalid)
    case "$zram_current_size" in ''|*[!0-9]*) echo "ANDROID_ZRAM_SWAP=unavailable reason=invalid_size device=$zram_dev"; return 0;; esac
    if [ "$zram_current_size" -eq 0 ]; then
        echo "$zram_target_bytes" > "$zram_size_path" 2>/dev/null || {
            echo "ANDROID_ZRAM_SWAP=unavailable reason=size_write_failed device=$zram_dev"
            return 0
        }
        zram_current_size=$(cat "$zram_size_path" 2>/dev/null || echo invalid)
    fi
    if [ "$zram_current_size" -lt "$zram_target_bytes" ]; then
        echo "ANDROID_ZRAM_SWAP=skipped reason=existing_device_too_small device=$zram_dev size_bytes=$zram_current_size"
        return 0
    fi
    if [ -x /sbin/mkswap ] && [ -x /sbin/swapon ] &&
       /sbin/mkswap "$zram_dev" >/dev/null 2>&1 && /sbin/swapon -p 5 "$zram_dev" 2>/dev/null; then
        echo "ANDROID_ZRAM_SWAP=enabled device=${zram_dev##*/} size_bytes=$zram_current_size priority=5"
    else
        echo "ANDROID_ZRAM_SWAP=unavailable reason=swap_setup_failed device=$zram_dev size_bytes=$zram_current_size"
    fi
}
setup_android_zram_swap
progress_status 5 '准备启动' 
# KOReader-style no-framework mode: stop Kindle's GUI and X display job. Xorg,
# awesome and blanket can outlive lab126_gui and keep writing the shared /dev/fb0.
# The external watchdog survives a killed launcher and restores both jobs.
status=$(/sbin/initctl status lab126_gui 2>&1 || true)
case "$status" in
  *'start/running'*) ;;
  *) echo "ANDROID_START_ERROR=lab126_gui_not_running status=$status"; exit 11;;
esac
x_status=$(/sbin/initctl status x 2>&1 || true)
case "$x_status" in
  *'start/running'*) ;;
  *) echo "ANDROID_START_ERROR=x_job_not_running status=$x_status"; exit 11;;
esac
# With the native GUI confirmed healthy, an old recovery frame must never
# prevent a new session or be replayed over the current native screen.
snapshot=/mnt/us/.kindle-a7-hwtcon-restore.bin
if [ -f "$snapshot" ]; then
    for proc in /proc/[0-9]*; do
        [ "$(cat "$proc/comm" 2>/dev/null || :)" != art-guard ] || { echo ANDROID_START_ERROR=android_still_active; exit 17; }
    done
    [ ! -L "$snapshot" ] || { echo ANDROID_START_ERROR=snapshot_symlink; exit 17; }
    mv -f "$snapshot" "$BASE/orphan-screen-snapshot.previous"
    echo ANDROID_ORPHAN_SCREEN_SNAPSHOT_ARCHIVED=1
fi
: > "$MARKER"
framework_stopped=1
if [ -x "$BASE/recover-aborted-session.sh" ]; then
    nohup setsid "$BASE/recover-aborted-session.sh" "$$" >> "$WATCHDOG" 2>&1 < /dev/null &
    echo $! > "$BASE/framework-watchdog.pid"
else
    # Keep older installations recoverable if the companion was not deployed.
    nohup setsid /bin/sh -c '
    BASE=/mnt/us/kindle-android
    while [ -e "$BASE/framework-stopped" ]; do
        pid=$(cat "$BASE/session.lock/pid" 2>/dev/null || :)
        if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
            sh "$BASE/restore-framework.sh" >> "$BASE/framework-watchdog.log" 2>&1
            exit $?
        fi
        sleep 2
    done
    ' >/dev/null 2>&1 < /dev/null &
    echo $! > "$BASE/framework-watchdog.pid"
fi
# USB polling stays active while Android is active or retained. It only writes
# the validated ART exit request and stops MTP if Kindle auto-started it.
if [ "$(cat "$BASE/native-framework.mode" 2>/dev/null || :)" = preserve ];then
    "$BASE/session-controller" native-hold "$$" || exit 14
    framework_stopped=1
    echo KINDLE_NATIVE_UI_PRESERVED=1
    # The runner snapshot is taken after the loading page was drawn. Restore
    # its memory without refreshing that old page; the native UI lease restores
    # the real Kindle frame before X resumes. The earlier crash watchdog keeps
    # its normal refresh fallback because it was spawned before this export.
    export KINDLE_RECOVERY_DEFER_REFRESH=1
else
if ! /sbin/stop lab126_gui; then
    echo ANDROID_START_ERROR=lab126_gui_stop_failed
    sh "$BASE/restore-framework.sh" || :
    exit 12
fi
stopped=0
i=0
while [ "$i" -lt "$POLL_LIMIT" ]; do
    status=$(/sbin/initctl status lab126_gui 2>&1 || true)
    case "$status" in *'stop/waiting'*) stopped=1; break;; esac
    i=$((i + 1)); poll_tick
done
[ "$stopped" = 1 ] || { echo "ANDROID_START_ERROR=lab126_gui_stop_unconfirmed status=$status"; sh "$BASE/restore-framework.sh" || :; exit 12; }
framework_stopped=1
echo KINDLE_FRAMEWORK_TEMPORARILY_STOPPED=lab126_gui

if ! /sbin/stop x; then
    echo ANDROID_START_ERROR=x_stop_failed
    sh "$BASE/restore-framework.sh" || :
    exit 13
fi
x_stopped=0
i=0
while [ "$i" -lt "$POLL_LIMIT" ]; do
    x_status=$(/sbin/initctl status x 2>&1 || true)
    case "$x_status" in *'stop/waiting'*) x_stopped=1; break;; esac
    i=$((i + 1)); poll_tick
done
[ "$x_stopped" = 1 ] || { echo "ANDROID_START_ERROR=x_stop_unconfirmed status=$x_status"; sh "$BASE/restore-framework.sh" || :; exit 13; }
echo KINDLE_DISPLAY_STACK_TEMPORARILY_STOPPED=x
fi
start_progress
available=$(df -Pk /mnt/us | tail -n 1 | awk '{print $4}')
[ "$available" -ge 300000 ] || { echo "ANDROID_START_ERROR=need_300000_kib_free available_kib=$available"; exit 4; }
progress_status 10 '检查系统文件'
now_ms(){ awk '{printf "%d",$1*1000}' /proc/uptime; }
cache_check_start=$(now_ms)
system_cache=0
cache_miss_reason=not_ready
cache_ready=$BASE/system-cache.ready
if [ -f "$cache_ready" ] && [ ! -L "$cache_ready" ] &&
   [ -f "$BASE/system-cache.img" ] && [ ! -L "$BASE/system-cache.img" ] &&
   [ -f "$BASE/system-cache-bootstrap.tar" ] && [ ! -L "$BASE/system-cache-bootstrap.tar" ] &&
   [ -f "$BASE/app-graphics-overlay.tar.gz.sha256" ] &&
   [ -f "$BASE/gralloc.hwtcon.so.sha256" ]; then
    expected_sha=$(awk '{print $1}' "$BASE/payload.sha256")
    key=$(cat "$BASE/payload.sha256" "$BASE/app-graphics-overlay.tar.gz.sha256" "$BASE/gralloc.hwtcon.so.sha256" | sha256sum | awk '{print $1}')
    if [ "$key" = "$(sed -n '1p' "$cache_ready")" ] &&
       [ "$expected_sha" = "$(sed -n '5p' "$cache_ready")" ] &&
       [ "$(stat -c %s "$BASE/system-cache.img")" = "$(sed -n '4p' "$cache_ready")" ] &&
       [ "$(digest_cache "$BASE/system-cache-bootstrap.tar" "$BASE/system-cache-bootstrap.sha-cache")" = "$(sed -n '3p' "$cache_ready")" ]; then
        if unshare -m --propagation private sh -c '
            set -eu
            image=$1; expected_key=$2
            mountpoint=$(mktemp -d /tmp/kindle-cache-check.XXXXXX)
            loop=
            mounted=0
            cleanup(){ [ "$mounted" = 0 ] || umount "$mountpoint"; [ -z "$loop" ] || losetup -d "$loop"; rmdir "$mountpoint"; }
            trap cleanup EXIT
            loop=$("$KINDLE_LOOP_DEVICE_HELPER")
            losetup -r "$loop" "$image"
            mount -t ext4 -o ro,nosuid,nodev "$loop" "$mountpoint"
            mounted=1
            [ "$(cat "$mountpoint/.kindle-system-cache-key")" = "$expected_key" ]
            [ -x "$mountpoint/bin/art-guard" ]
            [ -x "$mountpoint/bin/app_process32" ]
        ' sh "$BASE/system-cache.img" "$key"; then
            system_cache=1
            cache_miss_reason=
        else
            cache_miss_reason=readonly_mount_or_marker_check_failed
        fi
    else
        cache_miss_reason=key_size_or_bootstrap_mismatch
    fi
fi
cache_check_end=$(now_ms)
echo "ANDROID_SYSTEM_CACHE_FAST_CHECK_MS=$((cache_check_end-cache_check_start)) hit=$system_cache reason=${cache_miss_reason:-none}"
if [ "$system_cache" = 1 ]; then
    echo ANDROID_SYSTEM_CACHE_INTEGRITY=build_refresh_sha256_and_readonly_mount_probe
    echo ANDROID_PAYLOAD_SHA256_SKIPPED=verified_cache_hit
else
    echo ANDROID_SYSTEM_CACHE_MISS=$cache_miss_reason
    payload_check_start=$(now_ms)
    expected_sha=$(awk '{print $1}' "$BASE/payload.sha256")
    actual_sha=$(sha256sum "$PAYLOAD" | awk '{print $1}')
    [ "$actual_sha" = "$expected_sha" ] || { echo ANDROID_START_ERROR=payload_checksum_mismatch; exit 8; }
    echo "ANDROID_PAYLOAD_SHA256_MS=$(( $(now_ms)-payload_check_start ))"
fi
# Resolve backing files after orphan-loop cleanup, before attaching images.
KINDLE_DATA_IMAGE=$BASE/data.img
KINDLE_SYSTEM_CACHE_BACKING=$BASE/system-cache.img
if [ -x "$BASE/image-backing-path.sh" ];then
    KINDLE_DATA_IMAGE=$("$BASE/image-backing-path.sh" data.img)
    if [ "$system_cache" = 1 ];then KINDLE_SYSTEM_CACHE_BACKING=$("$BASE/image-backing-path.sh" system-cache.img);fi
fi
export KINDLE_DATA_IMAGE
stage=$(mktemp -d /mnt/us/kindle-a7-art.XXXXXX)
chmod 700 "$stage"
printf '%s\n' "$stage" > "$BASE/boot-progress.stage"
echo "ANDROID_SESSION_STAGE=$stage"
cleanup_status=active
# A full Kindle reboot already repainted the panel. Never replay a snapshot
# whose modification predates this boot; current-boot recovery is preserved.
snapshot=/mnt/us/.kindle-a7-hwtcon-restore.bin
if [ -f "$snapshot" ]; then
    boot_epoch=$(awk '/^btime / {print $2}' /proc/stat)
    snapshot_epoch=$(stat -c %Y "$snapshot")
    if [ "$snapshot_epoch" -lt "$boot_epoch" ]; then
        rm -f "$snapshot"
        echo ANDROID_STALE_PREBOOT_SNAPSHOT_REMOVED=1
    fi
fi
fixed_runtime=0
progress_status 25 '准备运行环境'
if [ "$system_cache" = 1 ]; then
    if [ -f "$BASE/fixed-runtime.ready" ] && [ ! -L "$BASE/fixed-runtime.ready" ] &&
       [ -f "$BASE/fixed-bootstrap.tar" ] && [ ! -L "$BASE/fixed-bootstrap.tar" ] &&
       [ "$(sed -n '1p' "$BASE/fixed-runtime.ready")" = FIXED_RUNTIME_V1 ] &&
       [ "$(sed -n '2p' "$BASE/fixed-runtime.ready")" = "$key" ] &&
       [ "$(digest_cache "$BASE/fixed-bootstrap.tar" "$BASE/fixed-bootstrap.sha-cache")" = "$(sed -n '3p' "$BASE/fixed-runtime.ready")" ];then
        tar -xf "$BASE/fixed-bootstrap.tar" -C "$stage"
        fixed_runtime=1
        echo ANDROID_FIXED_RUNTIME_HIT=1
    else
        tar -xf "$BASE/system-cache-bootstrap.tar" -C "$stage"
        echo ANDROID_FIXED_RUNTIME_HIT=0
    fi
    export KINDLE_SYSTEM_CACHE_IMAGE=$KINDLE_SYSTEM_CACHE_BACKING
    echo ANDROID_SYSTEM_CACHE_HIT=1
elif ! tar -xzf "$PAYLOAD" --no-same-owner -C "$stage"; then
    if [ -f "$stage/run.sh" ]; then sh "$stage/run.sh" "$stage" --cleanup-only || :; else rmdir "$stage" 2>/dev/null || :; fi
    echo ANDROID_START_ERROR=payload_extract_failed
    exit 5
fi
# Optional verified recovery helper can restore memory without replaying the
# boot screen. Native GUI paints its own screen after this container exits.
if [ -f "$BASE/hwtcon-frame-probe" ] && [ -f "$BASE/hwtcon-frame-probe.sha256" ]; then
    helper_expected=$(awk '{print $1}' "$BASE/hwtcon-frame-probe.sha256")
    helper_actual=$(sha256sum "$BASE/hwtcon-frame-probe" | awk '{print $1}')
    [ "$helper_expected" = "$helper_actual" ] || { echo ANDROID_START_ERROR=recovery_helper_checksum; exit 8; }
    cp "$BASE/hwtcon-frame-probe" "$stage/rootfs/tests/hwtcon-frame-probe"
    export KINDLE_RECOVERY_DEFER_REFRESH=1
fi
if [ "$system_cache" = 0 ]; then
if [ -f "$BASE/gralloc.hwtcon.so" ] && [ -f "$BASE/gralloc.hwtcon.so.sha256" ]; then
    (cd "$BASE" && sha256sum -c gralloc.hwtcon.so.sha256) || exit 8
    cp "$BASE/gralloc.hwtcon.so" "$stage/rootfs/system/lib/hw/gralloc.default.so"
fi
if [ -f "$BASE/app-graphics-overlay.tar.gz" ] && [ -f "$BASE/app-graphics-overlay.tar.gz.sha256" ]; then
    (cd "$BASE" && sha256sum -c app-graphics-overlay.tar.gz.sha256) || exit 8
    tar -xzf "$BASE/app-graphics-overlay.tar.gz" --no-same-owner -C "$stage"
    extra_overlay_files="$extra_overlay_files $(tar -tzf "$BASE/app-graphics-overlay.tar.gz")"
    [ -e "$stage/rootfs/etc" ] || ln -s system/etc "$stage/rootfs/etc"
    echo ANDROID_APP_GRAPHICS_SOFTWARE_LOADER=1
fi
else
    [ -e "$stage/rootfs/etc" ] || ln -s system/etc "$stage/rootfs/etc"
fi
if [ "$system_cache" = 0 ] && [ ! -e "$BASE/system-cache.disabled" ]; then
    progress_status 42 '首次准备系统缓存'
    if sh "$BASE/prepare-system-cache.sh" --from-stage "$stage"; then
        system_cache=1
        key=$(sed -n '1p' "$BASE/system-cache.ready")
        KINDLE_SYSTEM_CACHE_IMAGE=$("$BASE/image-backing-path.sh" system-cache.img)
        export KINDLE_SYSTEM_CACHE_IMAGE
        echo ANDROID_SYSTEM_CACHE_PREPARED_AT_FIRST_BOOT=1
    else
        echo ANDROID_SYSTEM_CACHE_PREPARE_FALLBACK=flat_system
    fi
fi
if [ "$fixed_runtime" = 0 ];then
# Preserve compiled framework services and replace only the alarm handler class.
alarm_anchor='SYSTEMSERVERCLASSPATH=/system/framework/services.jar'
if grep -Fq "$alarm_anchor" "$stage/run.sh"; then
    sed 's@SYSTEMSERVERCLASSPATH=/system/framework/services.jar@SYSTEMSERVERCLASSPATH=/system/framework/kindle-alarm-fallback.jar:/system/framework/services.jar@g' "$stage/run.sh" > "$stage/run-alarm-fix.sh"
    mv "$stage/run-alarm-fix.sh" "$stage/run.sh"
else
    echo ANDROID_START_ERROR=alarm_classpath_anchor_missing
    exit 8
fi
if [ -n "$extra_overlay_files" ] || [ "$system_cache" = 1 ]; then
    {
        printf '#!/bin/sh\n'
        for extra in $extra_overlay_files; do
            case "$extra" in *../*|/*) exit 8;; rootfs/system/*) printf 'rm -f "$stage/%s"\n' "$extra";; esac
        done
        printf '[ ! -L "$stage/rootfs/etc" ] || rm -f "$stage/rootfs/etc"\n'
        printf 'rm -f "$stage/overlay-cleanup.sh"\n'
    } > "$stage/overlay-cleanup.sh"
    sed '/^[[:space:]]*rmdir "\$stage"\/rootfs\/system /i\
 if [ -f "$stage/overlay-cleanup.sh" ]; then . "$stage/overlay-cleanup.sh"; fi
' "$stage/run.sh" > "$stage/run-with-overlay-cleanup.sh"
    mv "$stage/run-with-overlay-cleanup.sh" "$stage/run.sh"
    grep -Fq 'if [ -f "$stage/overlay-cleanup.sh" ]; then . "$stage/overlay-cleanup.sh"; fi' "$stage/run.sh" || {
        echo ANDROID_START_ERROR=overlay_cleanup_anchor_missing;exit 8;
    }
fi
if [ "$system_cache" = 1 ]; then
    cache_mount_anchor=' mount -t proc -o ro,nosuid,nodev,noexec proc "$root/proc"'
    [ "$(grep -Fc "$cache_mount_anchor" "$stage/run.sh" || :)" = 1 ] || exit 8
    awk '
    /^ mount -t proc -o ro,nosuid,nodev,noexec proc / {
      print " system_loop=$(losetup -f)"
      print " losetup -r \"$system_loop\" \"${KINDLE_SYSTEM_CACHE_IMAGE:?}\""
      print " echo \"$system_loop\" > \"$stage/system-loop-device\""
      print " mount -t ext4 -o ro,nosuid,nodev \"$system_loop\" \"$root/system\""
      print " echo ANDROID_SYSTEM_IMAGE_MOUNTED=read-only >&2"
    }
    /^ if \[ -f "\$stage\/data-loop-device" \];then/ {
      print " if [ -f \"$stage/system-loop-device\" ]; then"
      print "  system_loop=$(cat \"$stage/system-loop-device\")"
      print "  case \"$system_loop\" in /dev/loop[0-9]*) losetup -d \"$system_loop\" || return 95;; *) return 95;; esac"
      print "  rm -f \"$stage/system-loop-device\""
      print " fi"
    }
    {print}
    ' "$stage/run.sh" > "$stage/run-cache.sh"
    mv "$stage/run-cache.sh" "$stage/run.sh"
    sh -n "$stage/run.sh"
fi
# Preserve the data image while bypassing the MTP/FUSE file view.
if [ "$(grep -Fc 'data_image=/mnt/us/kindle-android/data.img' "$stage/run.sh" || :)" != 1 ];then
    echo ANDROID_START_ERROR=data_image_path_anchor_missing;exit 8
fi
sed 's@data_image=/mnt/us/kindle-android/data.img@data_image=${KINDLE_DATA_IMAGE:-/mnt/us/kindle-android/data.img}@' "$stage/run.sh" > "$stage/run-native-image.sh"
mv "$stage/run-native-image.sh" "$stage/run.sh"
# Firmware 5.20.1 may expose /dev/loop/N before /dev/loopN. Resolve and
# validate each newly allocated kernel device before the payload attaches it.
[ -x "$KINDLE_LOOP_DEVICE_HELPER" ] || { echo ANDROID_START_ERROR=kernel_device_helper_missing;exit 8; }
sed 's@$(losetup -f)@$("$KINDLE_LOOP_DEVICE_HELPER")@g' "$stage/run.sh" > "$stage/run-device-nodes.sh"
mv "$stage/run-device-nodes.sh" "$stage/run.sh"
sh -n "$stage/run.sh"
# WeRead's 32-bit WebView/native libraries reserve about 1 GiB of virtual
# address space. The probe payload carried a 1 GiB RLIMIT_AS, which makes a
# 2 MiB CursorWindow fail even when physical memory is available. Keep the
# session below ARM32's 3 GiB user VA ceiling while the kernel manages RAM.
limit_count=$(grep -c '^ ulimit -v 1048576$' "$stage/run.sh" || :)
if [ "$limit_count" = 1 ]; then
    sed 's/^ ulimit -v 1048576$/ ulimit -v 2621440/' "$stage/run.sh" > "$stage/run-with-address-space-limit.sh"
    chmod 0700 "$stage/run-with-address-space-limit.sh"
    mv "$stage/run-with-address-space-limit.sh" "$stage/run.sh"
    echo ANDROID_PROCESS_ADDRESS_LIMIT_KIB=2621440
elif [ "$(grep -c '^ ulimit -v 2621440$' "$stage/run.sh" || :)" = 1 ]; then
    echo ANDROID_PROCESS_ADDRESS_LIMIT_KIB=2621440 already_configured=1
else
    echo ANDROID_START_ERROR=unexpected_process_address_limit
    exit 8
fi
# Cleanup must work even after extraction consumes most free storage. Keep the
# guard for normal probes, but bypass only its free-space threshold in the
# already validated --cleanup-only mode.
space_guard_count=$(grep -Fc '[ "$available" -ge 262144 ]' "$stage/run.sh" || :)
if [ "$space_guard_count" = 1 ]; then
    sed 's@^\[ "$available" -ge 262144 \].*@ [ "${2:-}" = "--cleanup-only" ] || [ "$available" -ge 262144 ] || exit 40@' "$stage/run.sh" > "$stage/run-with-storage-guard.sh"
    chmod 0700 "$stage/run-with-storage-guard.sh"
    mv "$stage/run-with-storage-guard.sh" "$stage/run.sh"
    echo ANDROID_CLEANUP_STORAGE_GUARD=1
elif grep -Fq '"--cleanup-only"' "$stage/run.sh"; then
    echo ANDROID_CLEANUP_STORAGE_GUARD=1 already_configured=1
else
    echo ANDROID_START_ERROR=unexpected_storage_guard
    exit 8
fi
# Bind only the dedicated USB transfer folder inside this disposable mount namespace.
if [ -x "$BASE/session-preflight" ] && [ -f "$BASE/session-preflight.sha256" ]; then
    (cd "$BASE" && sha256sum -c session-preflight.sha256) || exit 8
    [ "$(grep -Fc "vnd_id=\$(stat -Lc" "$stage/run.sh" || :)" = 1 ] || { echo ANDROID_START_ERROR=binder_scan_anchor; exit 8; }
    awk '
    /^vnd_id=\$\(stat -Lc/ {print " /mnt/us/kindle-android/session-preflight --binder-idle"; skip=1; next}
    skip {if($0=="done")skip=0; next}
    {print}
    END {if(skip)exit 8}
    ' "$stage/run.sh" > "$stage/run-native-preflight.sh"
    mv "$stage/run-native-preflight.sh" "$stage/run.sh"
    echo ANDROID_BINDER_PREFLIGHT=native_checked
fi
# The host system partition and Android's private data remain separate.
[ ! -L /mnt/us/AndroidFiles ] || { echo ANDROID_START_ERROR=pc_files_symlink; exit 8; }
mkdir -p /mnt/us/AndroidFiles
[ "$(readlink -f /mnt/us/AndroidFiles)" = /mnt/us/AndroidFiles ] || exit 8
pc_anchor=' mount --bind "$root/data/local/tmp" "$root/tmp"'
[ "$(grep -Fc "$pc_anchor" "$stage/run.sh" || :)" = 1 ] || { echo ANDROID_START_ERROR=pc_files_mount_anchor; exit 8; }
awk '
{ print }
$0 == " mount --bind \"$root/data/local/tmp\" \"$root/tmp\"" {
 print " for desktop_apk in \"$root\"/data/app/org.kindlelab.apkprobe-*/base.apk; do"
 print "  [ -f \"$desktop_apk\" ] && [ ! -L \"$desktop_apk\" ] || continue"
 print "  if ! cmp -s \"$root/system/etc/kindlelab-apk-probe.apk\" \"$desktop_apk\"; then"
 print "   cp -p \"$desktop_apk\" \"$desktop_apk.boot-previous\""
 print "   cp \"$root/system/etc/kindlelab-apk-probe.apk\" \"$desktop_apk.boot-new\""
 print "   chown 1000:1000 \"$desktop_apk.boot-new\"; chmod 0644 \"$desktop_apk.boot-new\""
 print "   mv \"$desktop_apk.boot-new\" \"$desktop_apk\""
 print "   echo ANDROID_DESKTOP_CODE_SYNC=before_package_scan >&2"
 print "  fi"
 print " done"
 print " mkdir -p \"$root/data/media/0/PC\""
 print " mount --bind /mnt/us/AndroidFiles \"$root/data/media/0/PC\""
 print " mount -o remount,bind,nosuid,nodev,noexec \"$root/data/media/0/PC\""
 print " echo ANDROID_PC_FILES_BIND_READY=1 >&2"
}' "$stage/run.sh" > "$stage/run-with-pc-files.sh"
mv "$stage/run-with-pc-files.sh" "$stage/run.sh"
echo ANDROID_PC_FILES_HOST=/mnt/us/AndroidFiles
# Lifecycle request files live on persistent /data. Clear them immediately
# after mounting it, before starting Android services or accepting new input.
reset_anchor='  export KINDLE_ANDROID_DATA_PERSISTENT=1'
[ "$(grep -Fc "$reset_anchor" "$stage/run.sh" || :)" = 1 ] || { echo ANDROID_START_ERROR=warm_reset_anchor;exit 8; }
awk '
{print}
$0=="  export KINDLE_ANDROID_DATA_PERSISTENT=1" {
 print "  rm -f \"$root/data/local/tmp/kindle-android-exit\" \"$root/data/local/tmp/kindle-android-retain\" \"$root/data/local/tmp/kindle-warm-available\" \"$root/data/local/tmp/kindle-session-locked\" \"$root/data/local/tmp/kindle-input-release\" \"$root/data/local/tmp/kindle-input-released\" \"$root/data/local/tmp/kindle-input-acquired\" \"$root/data/local/tmp/kindle-input-resume-cutoff\" \"$root/data/local/tmp/kindle-display-quiesce\" \"$root/data/local/tmp/kindle-display-quiesced\" \"$root/data/local/tmp/kindle-display-resume\" \"$root/data/local/tmp/kindle-display-resumed\" \"$root/data/local/tmp/kindle-display-full-refresh\" \"$root/data/local/tmp/kindle-display-initial-frame-request\" \"$root/data/local/tmp/kindle-display-initial-frame-complete\" \"$root/data/local/tmp/kindle-page-direction\""
}' "$stage/run.sh" > "$stage/run-warm-reset.sh"
mv "$stage/run-warm-reset.sh" "$stage/run.sh"
else
    [ -x "$KINDLE_LOOP_DEVICE_HELPER" ] || exit 8
    (cd "$BASE" && sha256sum -c session-preflight.sha256) || exit 8
    [ ! -L /mnt/us/AndroidFiles ] || exit 8
    mkdir -p /mnt/us/AndroidFiles
    [ "$(readlink -f /mnt/us/AndroidFiles)" = /mnt/us/AndroidFiles ] || exit 8
    printf '%s\n' '#!/bin/sh' '[ ! -L "$stage/rootfs/etc" ] || rm -f "$stage/rootfs/etc"' 'rm -f "$stage/overlay-cleanup.sh"' > "$stage/overlay-cleanup.sh"
    echo ANDROID_RUNTIME_PREPARED_AT_INSTALL=1
fi
progress_status 55 '连接显示与触控' 
if [ -x "$BASE/session-power-guard" ]; then
    nohup "$BASE/session-power-guard" "$stage" >> "$BASE/power-guard.log" 2>&1 < /dev/null &
    power_guard_pid=$!
fi



if [ -x "$BASE/slirp4netns" ] && [ -x "$BASE/network-relay.sh" ]; then
    : > "$BASE/network-relay.log"
    nohup "$BASE/network-relay.sh" "$stage" >> "$BASE/network-relay.log" 2>&1 < /dev/null &
    network_relay_pid=$!
    echo "ANDROID_NETWORK_RELAY_STARTED=1 pid=$network_relay_pid"
else
    echo ANDROID_NETWORK_RELAY_UNAVAILABLE=1
fi

set +e
progress_status 58 '正在载入系统'
if [ -x "$BASE/session-freezer" ] && [ -x "$BASE/warm-session.sh" ];then
    nohup "$BASE/warm-session.sh" "$$" "$stage" >> "$BASE/warm-session.log" 2>&1 < /dev/null &
    warm_session_pid=$!
    echo "$warm_session_pid" > "$BASE/warm-controller.pid"
fi
sh "$stage/run.sh" "$stage" --android-session-probe > "$BASE/session-result.tar"
run_rc=$?
set -e
stop_progress
if [ -f "$BASE/session-result.tar" ]; then
    tar -xOf "$BASE/session-result.tar" result.txt >> "$BASE/session.log" 2>&1 || :
    tar -xOf "$BASE/session-result.tar" exit-code.txt >> "$BASE/session.log" 2>&1 || :
    cp "$BASE/session-result.tar" "$BASE/session-result.latest.tar" 2>/dev/null || :
fi
rm -f "$BASE/session-result.tar"
if [ -d "$stage" ] && [ -f "$stage/hwtcon-backup-ready" ]; then
    echo ANDROID_SESSION_RECOVERY_RETRY=1
    sh "$stage/run.sh" "$stage" --cleanup-only || :
fi
# The base payload cleanup predates the optional native-library overlay.
# Remove only its explicitly known system-app files after all mounts are gone.
if [ -d "$stage" ] && ! awk -v p="$stage" '$2==p || index($2,p"/")==1 {found=1} END {exit !found}' /proc/mounts; then
    for extra in $extra_overlay_files; do
        case "$extra" in *../*|/*) echo ANDROID_EXTRA_CLEANUP_PATH_REJECTED=1;; rootfs/system/*) rm -f "$stage/$extra";; esac
    done
    [ ! -L "$stage/rootfs/etc" ] || rm -f "$stage/rootfs/etc"
    rm -f "$stage/overlay-cleanup.sh"
    find "$stage" -depth -type d -exec rmdir {} \; 2>/dev/null || :
fi
if [ -d "$stage" ]; then
    cleanup_status=preserved_for_recovery
    echo "ANDROID_SESSION_RECOVERY_REQUIRED=1 stage=$stage"
    echo ANDROID_SESSION_HOME_LAUNCH_SKIPPED=1
    exit 6
fi
if [ -d "$stage" ]; then cleanup_status=preserved_after_cleanup_attempt; else cleanup_status=removed; fi
if ! sh "$BASE/restore-framework.sh"; then
    echo ANDROID_SESSION_FRAMEWORK_RESTORE_FAILED=1
    exit 7
fi
framework_stopped=0
echo "ANDROID_SESSION_EXITED=1 run_rc=$run_rc"
home_launch_pending=1
exit "$run_rc"
