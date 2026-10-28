#!/bin/sh
# Run Kindle's normal MTP responder in a private mount namespace. The host can
# transfer files to /mnt/us/AndroidFiles, while /mnt/us/kindle-android is an
# empty in-memory directory from MTP's point of view.
set -eu
BASE=/mnt/us/kindle-android
EXPORT=/dev/shm/kindle-android-mtp-export
PIDFILE=$BASE/frozen-mtp.pid
LOG=$BASE/frozen-mtp.log

alive() {
 pid=$1; expected=$2
 case "$pid:$expected" in *[!0-9:]*) return 1;; esac
 [ -r "/proc/$pid/stat" ] || return 1
 actual=$(sed 's/^.*) //' "/proc/$pid/stat" | awk '{print $20}')
 [ "$actual" = "$expected" ]
}

case "${1:-}" in
--run)
 [ "$#" -eq 1 ] || exit 2
 [ -d "$EXPORT/kindle-android" ] || exit 3
 mount --bind "$EXPORT/kindle-android" /mnt/us/kindle-android
 : > "$EXPORT/namespace-mounted"
 exec /usr/bin/tizen-mtp -f
 ;;
start)
 [ "$(head -n 1 "$BASE/warm-state" 2>/dev/null || :)" = native ] || { echo FROZEN_MTP_REFUSED=not_native_state; exit 4; }
 [ "$(cat /sys/fs/cgroup/kindle-android-warm/session/freezer.state 2>/dev/null || :)" = FROZEN ] || { echo FROZEN_MTP_REFUSED=android_not_frozen; exit 5; }
 [ -f "$BASE/mtp-held" ] || { echo FROZEN_MTP_REFUSED=mtp_gate_not_held; exit 6; }
 if [ -r "$PIDFILE" ]; then
  old_pid=$(sed -n '1p' "$PIDFILE"); old_start=$(sed -n '2p' "$PIDFILE")
  if alive "$old_pid" "$old_start"; then
   old_exe=$(readlink "/proc/$old_pid/exe" 2>/dev/null || :)
   old_comm=$(cat "/proc/$old_pid/comm" 2>/dev/null || :)
   [ "$old_exe" = /usr/bin/tizen-mtp ] || [ "$old_comm" = tizen-mtp ] || { echo FROZEN_MTP_PID_BUSY=1; exit 7; }
   echo FROZEN_MTP_ALREADY_RUNNING=1; exit 0
  fi
  rm -f "$PIDFILE"
 fi
 [ ! -e "$EXPORT" ] || { echo FROZEN_MTP_EXPORT_STALE=1; exit 7; }
 mkdir -m 700 "$EXPORT"
 mkdir -m 755 "$EXPORT/kindle-android"
 (
  exec >> "$LOG" 2>&1
  exec unshare -m --propagation private /bin/sh "$BASE/frozen-mtp-export.sh" --run
 ) </dev/null >/dev/null 2>&1 &
 pid=$!
 start=$(sed 's/^.*) //' "/proc/$pid/stat" 2>/dev/null | awk '{print $20}')
 case "$start" in ''|*[!0-9]*) echo FROZEN_MTP_START_FAILED=pid_identity; exit 8;; esac
 printf '%s\n%s\n' "$pid" "$start" > "$PIDFILE.new"
 chmod 600 "$PIDFILE.new"
 mv "$PIDFILE.new" "$PIDFILE"
 n=0
 while [ "$n" -lt 80 ]; do
  if ! kill -0 "$pid" 2>/dev/null; then
   rm -f "$PIDFILE"
   rm -f "$EXPORT/namespace-mounted"
   rmdir "$EXPORT/kindle-android" "$EXPORT" 2>/dev/null || :
   echo FROZEN_MTP_START_FAILED=process_exited
   exit 8
  fi
  exe=$(readlink "/proc/$pid/exe" 2>/dev/null || :)
  comm=$(cat "/proc/$pid/comm" 2>/dev/null || :)
  if { [ "$exe" = /usr/bin/tizen-mtp ] || [ "$comm" = tizen-mtp ]; } && [ -f "$EXPORT/namespace-mounted" ]; then
   echo FROZEN_MTP_EXPORT_STARTED=1 pid=$pid
   exit 0
  fi
  n=$((n+1)); usleep 100000
 done
 kill -TERM "$pid" 2>/dev/null || :
 echo FROZEN_MTP_START_FAILED=timeout
 exit 9;;
stop)
 if [ -r "$PIDFILE" ]; then
  pid=$(sed -n '1p' "$PIDFILE"); expected=$(sed -n '2p' "$PIDFILE")
  if alive "$pid" "$expected"; then
   [ -x "$BASE/usb-session-guard" ] || { echo FROZEN_MTP_STOP_REFUSED=usb_guard_missing; exit 24; }
   if ! "$BASE/usb-session-guard" --check; then
    echo FROZEN_MTP_STOP_DEFERRED_USB_CONNECTED=1
    exit 24
   fi
   kill -TERM "$pid" || { echo FROZEN_MTP_STOP_FAILED=signal; exit 10; }
   n=0
   # USB is already disconnected. Bound the idle responder's TERM grace to
   # 300 ms; recheck USB and the same PID identity before the KILL fallback.
   while alive "$pid" "$expected" && [ "$n" -lt 3 ]; do n=$((n+1));usleep 100000;done
   if alive "$pid" "$expected"; then
    # Kindle's FunctionFS responder can remain stuck in an endpoint wait after
    # unplug and ignore TERM. It is safe to force-stop only after rechecking
    # that no USB host is attached; otherwise preserve the process/session.
    "$BASE/usb-session-guard" --check || { echo FROZEN_MTP_STOP_DEFERRED_USB_CONNECTED=1; exit 24; }
    echo FROZEN_MTP_TERM_STUCK_FORCE_KILL=1 pid=$pid
    kill -KILL "$pid" || { echo FROZEN_MTP_STOP_FAILED=kill; exit 10; }
    n=0
    while alive "$pid" "$expected" && [ "$n" -lt 30 ]; do n=$((n+1));usleep 100000;done
    alive "$pid" "$expected" && { echo FROZEN_MTP_STOP_FAILED=kill_timeout; exit 11; }
   fi
  fi
  rm -f "$PIDFILE"
 fi
 rm -f "$EXPORT/namespace-mounted"
 if [ -d "$EXPORT/kindle-android" ]; then rmdir "$EXPORT/kindle-android" 2>/dev/null || { echo FROZEN_MTP_EXPORT_CLEANUP_FAILED=1; exit 12; }; fi
 rmdir "$EXPORT" 2>/dev/null || { [ ! -e "$EXPORT" ] || { echo FROZEN_MTP_EXPORT_CLEANUP_FAILED=1; exit 12; }; }
 echo FROZEN_MTP_EXPORT_STOPPED=1;;
*) exit 2;;
esac
