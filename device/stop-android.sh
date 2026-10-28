#!/bin/sh
# Name: 彻底退出 RetyOs
# Author: Kindle Android DIY
# DontUseFBInk
set -eu
BASE=/mnt/us/kindle-android
[ -d "$BASE/session.lock" ] || exit 0
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
