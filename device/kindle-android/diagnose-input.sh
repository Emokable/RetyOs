#!/bin/sh
# Read-only report: no grabs, input injection, pairing changes or service restart.
BASE=/mnt/us/kindle-android
echo RETYOS_INPUT_DIAGNOSTIC_V1
date
for name in warm-state warm-power.state native-ui.lease; do
 echo "--- $name"
 cat "$BASE/$name" 2>/dev/null
done
echo '--- kernel input devices'
cat /proc/bus/input/devices
echo '--- native jobs'
for job in x lab126_gui input_manager btmanagerd; do
 /sbin/initctl status "$job" 2>/dev/null
done
echo '--- input owners (no command arguments)'
for proc in /proc/[0-9]*; do
 for fd in "$proc"/fd/*; do
  target=$(readlink "$fd" 2>/dev/null) || continue
  case "$target" in
   /dev/input/event*|/dev/input/uinput|/dev/uhid|/dev/uinput)
    printf 'pid=%s comm=%s fd=%s device=%s state=' "${proc##*/}" "$(cat "$proc/comm" 2>/dev/null)" "${fd##*/}" "$target"
    sed 's/^.*) //' "$proc/stat" 2>/dev/null | cut -d ' ' -f 1
    ;;
  esac
 done
done
echo '--- input handoff log'
tail -n 60 "$BASE/warm-session.log" 2>/dev/null
echo RETYOS_INPUT_DIAGNOSTIC_END
exit 0
