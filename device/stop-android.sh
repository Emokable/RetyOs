#!/bin/sh
# Name: 彻底退出 RetyOs
# Author: Kindle Android DIY
# DontUseFBInk
set -eu
BASE=/mnt/us/kindle-android
exec >> "$BASE/stop-entry.log" 2>&1
date
echo ANDROID_STOP_ENTRY_RECEIVED=1
[ -d "$BASE/session.lock" ] || { echo ANDROID_STOP_NO_LIVE_SESSION=1;exit 0; }
# Also use the guard's existing persistent exit marker. Unlike warm.command,
# it cannot be overwritten by a resume/retain request before consumption.
stage=$(sed -n '2p' "$BASE/warm-state" 2>/dev/null || :)
request_exit_for_guard() {
 pid=$1
 exit_dir="/proc/$pid/root/data/local/tmp"
 [ -d "$exit_dir" ] || mkdir -p "$exit_dir" 2>/dev/null || :
 if [ -d "$exit_dir" ] && printf 'document-stop\n' > "$exit_dir/kindle-android-exit" 2>/dev/null;then
  echo ANDROID_STOP_PERSISTENT_REQUEST=1
  return 0
 fi
 # Some retained kernels expose the persistent image's local/tmp at /tmp
 # after /data has been detached. Use this alias only when it is a mounted dir.
 if [ -d "/proc/$pid/root/tmp" ] && grep -Fq ' /tmp ' "/proc/$pid/mountinfo" 2>/dev/null &&
    printf 'document-stop\n' > "/proc/$pid/root/tmp/kindle-android-exit" 2>/dev/null;then
  echo ANDROID_STOP_PERSISTENT_REQUEST=1 path=tmp-alias
  return 0
 fi
 echo ANDROID_STOP_REQUEST_WRITE_FAILED=1 pid=$pid
 return 1
}
case "$stage" in
 /mnt/us/kindle-a7-art.??????)
  guard=$("$BASE/session-freezer" guard "$stage" 2>/dev/null || :)
  case "$guard" in ''|*[!0-9]*) :;; *)
   if [ "$(readlink "/proc/$guard/root" 2>/dev/null || :)" = "$stage/rootfs" ];then
    request_exit_for_guard "$guard" || :
   fi;;
  esac;;
esac
controller=$(cat "$BASE/warm-controller.pid" 2>/dev/null || :)
case "$controller" in ''|*[!0-9]*) controller=0;; esac
if [ "$controller" = 0 ] || ! kill -0 "$controller" 2>/dev/null;then
 stage=$(cat "$BASE/boot-progress.stage" 2>/dev/null || :)
 case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) exit 3;; esac
 if [ -f "$BASE/warm-native" ];then
  : > "$BASE/framework-stopped"
  /sbin/stop lab126_gui || :
  /sbin/stop x || :
 fi
 "$BASE/session-freezer" thaw "$stage" || exit 4
 guard=$("$BASE/session-freezer" guard "$stage")
  request_exit_for_guard "$guard" || exit 5
 exit 0
fi
printf 'exit\n' > "$BASE/warm.command.new"
mv "$BASE/warm.command.new" "$BASE/warm.command"
echo ANDROID_FULL_EXIT_REQUESTED=1
n=0
while [ "$n" -lt 30 ];do
 state=$(head -n1 "$BASE/warm-state" 2>/dev/null || :)
 if [ "$state" = exiting ] || [ ! -d "$BASE/session.lock" ];then
  echo "ANDROID_STOP_ACKNOWLEDGED=1 state=${state:-finished}"
  exit 0
 fi
 usleep 100000;n=$((n+1))
done
echo ANDROID_STOP_ACK_PENDING=1
