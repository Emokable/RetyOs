#!/bin/sh
# Native UI activation after Android display/input ownership has been released.
set -eu
BASE=/mnt/us/kindle-android
stage=$(cat "$BASE/warm-freezer.stage" 2>/dev/null || :)
if [ -n "$stage" ] && "$BASE/session-freezer" guard "$stage" >/dev/null 2>&1;then
 [ "$("$BASE/session-freezer" status "$stage" 2>/dev/null || :)" = FROZEN ] || {
  echo NATIVE_HOME_REFUSED_ANDROID_STILL_ACTIVE=1;exit 2;
 }
fi
clock(){ "$BASE/session-freezer" now; }
begin=$(clock)
trace_id=$(cat "$BASE/native-home.trace-id" 2>/dev/null || :)
case "$trace_id" in ''|*[!0-9]*) trace_id=$begin;;esac
check_only=0
poll_limit=35
[ "${1:-}" != --wait ] || poll_limit=300
[ "${1:-}" != --check ] || check_only=1
if [ "$check_only" = 1 ];then
 app=$(lipc-get-prop -s com.lab126.appmgrd activeApp 2>/dev/null || :)
 case "$app" in com.lab126.KPPMainApp|com.lab126.booklet.home) :;;*) exit 1;;esac
 active_ns=$(clock)
 "$BASE/native-frame-probe" >/dev/null || exit 1
 frame_ns=$(clock)
 echo "KINDLE_LATENCY_TRACE event=native_home_frame_valid trace_id=$trace_id monotonic_ns=$frame_ns app=$app passive_check=1"
 /var/local/kmc/bin/fbink -q -w -f -s || exit 4
 end=$(clock)
 printf 'trace_id=home-%s\nrequest_ns=%s\napp_active_ns=%s\nframe_valid_ns=%s\nrefresh_complete_ns=%s\napp=%s\n' \
  "$trace_id" "$trace_id" "$active_ns" "$frame_ns" "$end" "$app" > "$BASE/native-ui.ready.new"
 mv "$BASE/native-ui.ready.new" "$BASE/native-ui.ready"
 echo "KINDLE_LATENCY_TRACE event=native_home_refresh_complete trace_id=home-$trace_id request_ns=$trace_id app_active_ns=$active_ns frame_valid_ns=$frame_ns monotonic_ns=$end app=$app"
 echo "NATIVE_HOME_REFRESH_COMPLETE monotonic_ns=$end app=$app"
 exit 0
fi
rm -f "$BASE/native-ui.ready"
 echo "KINDLE_LATENCY_TRACE event=native_home_begin trace_id=home-$trace_id request_ns=$trace_id monotonic_ns=$begin"
attempt=0
home_active_logged=0
while [ "$attempt" -lt 1 ]; do
 attempt=$((attempt+1))
 app=$(lipc-get-prop -s com.lab126.appmgrd activeApp 2>/dev/null || :)
 if [ "$app" = tech.hackerdude.shell_integration.launcher ];then
  pid=$(lipc-get-prop -i com.lab126.appmgrd activeAppPid 2>/dev/null || :)
  case "$pid" in ''|*[!0-9]*) :;; *)
   command=$(tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null || :)
   case "$command" in /var/local/kmc/bin/sh_integration_launcher*)
    if [ -r "/proc/$pid/task/$pid/children" ] && [ -z "$(cat "/proc/$pid/task/$pid/children")" ];then
     # Same appmgrd stop request as the launcher's own completed-child path.
     lipc-set-prop com.lab126.appmgrd stop "$app" || :
     echo "NATIVE_ENTRY_LAUNCHER_RELEASE_REQUESTED pid=$pid monotonic_ns=$(clock)"
    fi;;
   esac;;
  esac
 fi
 if lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.KPPMainApp?view=KPP_HOME';then
  echo "NATIVE_HOME_REQUEST_ACCEPTED attempt=$attempt monotonic_ns=$(clock)"
 fi
 count=0
 while [ "$count" -lt "$poll_limit" ];do
  app=$(lipc-get-prop -s com.lab126.appmgrd activeApp 2>/dev/null || :)
  case "$app" in com.lab126.KPPMainApp|com.lab126.booklet.home)
   if [ "$home_active_logged" = 0 ];then
    active_ns=$(clock)
    echo "KINDLE_LATENCY_TRACE event=native_home_active trace_id=home-$trace_id monotonic_ns=$active_ns app=$app"
    home_active_logged=1
   fi
   if "$BASE/native-frame-probe";then
    frame_ns=$(clock)
    echo "KINDLE_LATENCY_TRACE event=native_home_frame_valid trace_id=home-$trace_id monotonic_ns=$frame_ns app=$app"
    # Re-submit existing pixels; do not clear or draw over the native page.
    if /var/local/kmc/bin/fbink -q -w -f -s;then
     end=$(clock)
     printf 'trace_id=home-%s\nrequest_ns=%s\napp_active_ns=%s\nframe_valid_ns=%s\nrefresh_complete_ns=%s\napp=%s\n' \
      "$trace_id" "$trace_id" "$active_ns" "$frame_ns" "$end" "$app" > "$BASE/native-ui.ready.new"
     mv "$BASE/native-ui.ready.new" "$BASE/native-ui.ready"
     echo "KINDLE_LATENCY_TRACE event=native_home_refresh_complete trace_id=home-$trace_id request_ns=$trace_id app_active_ns=$active_ns frame_valid_ns=$frame_ns monotonic_ns=$end duration_ms=$(((end-trace_id)/1000000)) app=$app"
     echo "NATIVE_HOME_REFRESH_COMPLETE monotonic_ns=$end app=$app"
     exit 0
    fi
    echo NATIVE_HOME_REFRESH_FAILED=1
    exit 4
   fi;;
  esac
  count=$((count+1));usleep 200000
 done
done
echo "NATIVE_HOME_READY_TIMEOUT app=${app:-unknown} monotonic_ns=$(clock)"
exit 3
