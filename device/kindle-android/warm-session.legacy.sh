#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
launcher=${1:-}
stage=${2:-}
case "$launcher" in ''|*[!0-9]*) exit 2;; esac
case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) exit 2;; esac
guard=
tmp=
current=starting
operation=none
operation_start=0
native_check_tick=0
native_checks=0
clock(){ "$BASE/session-freezer" now; }
state(){ current=$1; printf '%s\n%s\n%s\n%s\n' "$current" "$stage" "$launcher" "${guard:-0}" > "$BASE/warm-state.new"; mv "$BASE/warm-state.new" "$BASE/warm-state"; echo "WARM_SESSION_STATE=$current monotonic_ns=$(clock) operation=$operation"; }
phase(){ echo "WARM_PHASE=$1 monotonic_ns=$(clock) operation=$operation"; }
transition_begin(){
 operation=$1;detected=$(clock);operation_start=$2
 case "$operation_start" in ''|*[!0-9]*) operation_start=$detected;;esac
 if [ "${#operation_start}" -gt 18 ];then operation_start=$detected;fi
 if [ "$operation_start" -gt "$detected" ] || [ $((detected-operation_start)) -gt 60000000000 ];then operation_start=$detected;fi
 echo "WARM_TRANSITION_BEGIN kind=$operation requested_ns=$operation_start detected_ns=$detected"
}
transition_ready(){ end=$(clock);echo "WARM_TRANSITION_SOFTWARE_READY kind=$operation duration_ms=$(((end-operation_start)/1000000)) monotonic_ns=$end"; }
wait_file(){ path=$1;n=0;while [ "$n" -lt 50 ];do [ ! -f "$path" ] || return 0;kill -0 "$guard" 2>/dev/null || return 1;usleep 100000;n=$((n+1));done;return 1; }
wait_file_timed(){ name=$1;path=$2;begin=$($BASE/session-freezer now);phase "${name}_WAIT_BEGIN";if wait_file "$path";then rc=0;else rc=1;fi;end=$($BASE/session-freezer now);echo "WARM_PHASE=${name}_WAIT_END duration_ms=$(((end-begin)/1000000)) result=$rc";return "$rc"; }
stop_native(){
 phase NATIVE_GUI_STOP_BEGIN
 : > "$BASE/framework-stopped"
 for job in lab126_gui x; do
  job_begin=$(clock)
  phase "${job}_STOP_BEGIN"
  status=$(/sbin/initctl status "$job" 2>&1 || :)
  status_before=$(echo "$status" | tr ' \r\n' '___')
  stop_rc=0
  case "$status" in
   *start/running*) /sbin/stop "$job" || stop_rc=$?;;
   *stop/waiting*) :;;
   *) stop_rc=1;;
  esac
  status_after=$(/sbin/initctl status "$job" 2>&1 || :)
  status_after_log=$(echo "$status_after" | tr ' \r\n' '___')
  job_end=$(clock)
  echo "WARM_NATIVE_STOP_RESULT job=$job elapsed_ms=$(((job_end-job_begin)/1000000)) rc=$stop_rc before=$status_before after=$status_after_log"
  [ "$stop_rc" -eq 0 ] || return 1
  case "$status_after" in *stop/waiting*) :;; *) return 1;; esac
  phase "${job}_STOP_DONE"
 done
 for job in lab126_gui x;do case "$(/sbin/initctl status "$job" 2>&1)" in *stop/waiting*) :;; *) return 1;; esac;done
 phase NATIVE_GUI_STOP_DONE
}
request_exit(){
 if [ "$current" != exiting ];then clock > "$BASE/full-exit.started-ns";fi
 state exiting
 [ -z "$tmp" ] || rm -f "$tmp/kindle-warm-available"
 # Stop native drawing before normal container snapshot/teardown runs.
 if [ -n "$tmp" ] && [ -d "$tmp" ]; then
  if [ -f "$BASE/warm-native" ];then stop_native || :;fi
  "$BASE/session-freezer" thaw "$stage" || :
  printf 'warm-controller-exit\n' > "$tmp/kindle-android-exit"
 fi
}
abort(){ trap - EXIT HUP INT TERM; request_exit || :; exit 0; }
trap abort HUP INT TERM
state starting
for n in $(seq 1 1800); do
 kill -0 "$launcher" 2>/dev/null || exit 3
 guard=$("$BASE/session-freezer" guard "$stage" 2>/dev/null || :)
 [ -z "$guard" ] || tmp=/proc/$guard/root/data/local/tmp
 [ -z "$guard" ] || break
 usleep 100000
done
[ -n "$guard" ] || exit 4
"$BASE/session-freezer" prepare "$stage" || exit 5
while ! grep -q 'APK_ACTIVITY_PROCESS_LAUNCH_PASS=1' "$stage/result.txt" 2>/dev/null;do
 kill -0 "$launcher" 2>/dev/null && kill -0 "$guard" 2>/dev/null || exit 6
 usleep 200000
done
rm -f "$tmp/kindle-android-retain"
: > "$tmp/kindle-warm-available"
state active
return_native(){
 transition_begin retain "$(cat "$tmp/kindle-android-retain" 2>/dev/null || :)"
 printf '%s\n' "$operation_start" > "$BASE/native-home.trace-id.new"
 mv "$BASE/native-home.trace-id.new" "$BASE/native-home.trace-id"
 state retaining
 phase RETAIN_INPUT_AND_DISPLAY_LOCK_BEGIN
 rm -f "$tmp/kindle-display-quiesced" "$tmp/kindle-input-released" "$tmp/kindle-display-resumed"
 : > "$tmp/kindle-session-locked" || return 1
 : > "$tmp/kindle-input-release" || return 1
 : > "$tmp/kindle-display-quiesce" || return 1
 wait_file_timed DISPLAY_QUIESCE "$tmp/kindle-display-quiesced" || return 1
 wait_file_timed INPUT_RELEASE "$tmp/kindle-input-released" || return 1
 phase POWER_GUARD_NATIVE_WAIT_BEGIN
 : > "$BASE/warm-native" || return 1
 # The external power guard releases the native power key before freezing.
 n=0;while [ "$(cat "$BASE/warm-power.state" 2>/dev/null || :)" != native ];do n=$((n+1));[ "$n" -lt 50 ] || return 1;usleep 100000;done
 phase POWER_GUARD_NATIVE_WAIT_DONE
 phase FREEZER_FREEZE_BEGIN
 "$BASE/session-freezer" freeze "$stage" || return 1
 phase FREEZER_FREEZE_DONE
 phase NATIVE_FRAMEWORK_RESTORE_BEGIN
 sh "$BASE/restore-framework.sh" || return 1
 phase NATIVE_FRAMEWORK_RESTORE_DONE
 rm -f "$tmp/kindle-android-retain"
 state native_pending
 native_checks=0
 if sh "$BASE/activate-native-home.sh";then
  state native
  if sh "$BASE/storage-gate.sh" open-frozen;then
   phase MTP_FROZEN_EXPORT_READY
  else
   echo MTP_FROZEN_EXPORT_UNAVAILABLE=1
  fi
  transition_ready
 else
  echo NATIVE_HOME_CONFIRMATION_FAILED_SESSION_RETAINED=1
 fi
}
resume_android(){
 transition_begin resume "$(head -n1 "$BASE/entry-request.started-ns" 2>/dev/null || :)"
 if [ -x "$BASE/usb-session-guard" ] && ! "$BASE/usb-session-guard" --check;then
  echo WARM_RESUME_DEFERRED_USB_CONNECTED=1
  return 20
 fi
 rm -f "$BASE/native-home.trace-id" "$BASE/native-ui.ready"
 state resuming
 phase ANDROID_RESUME_BEGIN
 sh "$BASE/storage-gate.sh" close-frozen || return 1
 stop_native || return 1
 rm -f "$BASE/warm-native" "$tmp/kindle-input-acquired"
 phase FREEZER_THAW_BEGIN
 "$BASE/session-freezer" thaw "$stage" || return 1
 phase FREEZER_THAW_DONE
 phase POWER_GUARD_ANDROID_WAIT_BEGIN
 n=0;while [ "$(cat "$BASE/warm-power.state" 2>/dev/null || :)" != android ];do n=$((n+1));[ "$n" -lt 50 ] || return 1;usleep 100000;done
 phase POWER_GUARD_ANDROID_WAIT_DONE
 rm -f "$tmp/kindle-input-release"
 wait_file_timed INPUT_ACQUIRE "$tmp/kindle-input-acquired" || return 1
 "$BASE/session-freezer" now > "$tmp/kindle-input-resume-cutoff" || return 1
 rm -f "$tmp/kindle-display-quiesce" "$tmp/kindle-display-quiesced"
 : > "$tmp/kindle-display-full-refresh" || return 1
 : > "$tmp/kindle-display-resume" || return 1
 rm -f "$tmp/kindle-session-locked" || return 1
 wait_file_timed DISPLAY_FIRST_FRAME "$tmp/kindle-display-resumed" || return 1
 state active
 transition_ready
 nsenter -t "$guard" -U -n -m -r -w /system/bin/controls-client network >/dev/null 2>&1 &
}
while kill -0 "$launcher" 2>/dev/null && kill -0 "$guard" 2>/dev/null;do
 [ "$(cat "$BASE/session.lock/pid" 2>/dev/null || :)" = "$launcher" ] || break
 if [ -f "$tmp/kindle-android-exit" ] || [ -f "/proc/$guard/root/data/user_de/0/org.kindlelab.apkprobe/files/kindle-android-exit" ];then request_exit;exit 0;fi
 command=
 if [ -f "$BASE/warm.command" ];then command=$(cat "$BASE/warm.command");rm -f "$BASE/warm.command";fi
 if [ "$command" = exit ];then phase FULL_EXIT_REQUEST;request_exit;exit 0;fi
 if [ "$current" = native ] || [ "$current" = native_pending ];then
  if [ "$command" = resume ];then
   if resume_android;then :;else resume_rc=$?;if [ "$resume_rc" -ne 20 ];then echo WARM_RESUME_FAILED_NORMAL_EXIT=1;request_exit;exit 6;fi;fi
  fi
 elif [ "$current" = active ] && [ -f "$tmp/kindle-android-retain" ];then
  phase RETAIN_REQUEST_DETECTED
  if ! return_native;then echo WARM_RETAIN_FAILED_NORMAL_EXIT=1;request_exit;exit 7;fi
 fi
 if [ "$current" = native_pending ] && [ "$native_checks" -lt 120 ];then
  native_check_tick=$((native_check_tick+1))
  if [ "$native_check_tick" -ge 5 ];then
   native_check_tick=0;native_checks=$((native_checks+1))
   if sh "$BASE/activate-native-home.sh" --check;then state native;transition_ready;fi
   [ "$native_checks" -ne 120 ] || echo NATIVE_HOME_PASSIVE_CONFIRMATION_TIMEOUT=1
  fi
 fi
 usleep 200000
done
request_exit
