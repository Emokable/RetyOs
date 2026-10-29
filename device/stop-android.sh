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
case "$stage" in
 /mnt/us/kindle-a7-art.??????)
  guard=$("$BASE/session-freezer" guard "$stage" 2>/dev/null || :)
  case "$guard" in ''|*[!0-9]*) :;; *)
   if [ "$(readlink "/proc/$guard/root" 2>/dev/null || :)" = "$stage/rootfs" ];then
    printf 'document-stop\n' > "/proc/$guard/root/data/local/tmp/kindle-android-exit"
    echo ANDROID_STOP_PERSISTENT_REQUEST=1
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
 printf 'manual-full-exit\n' > "/proc/$guard/root/data/local/tmp/kindle-android-exit"
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
