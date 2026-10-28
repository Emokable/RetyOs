#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
MARKER=$BASE/framework-stopped
phase(){ echo "NATIVE_RESTORE_PHASE=$1 monotonic_ns=$($BASE/session-freezer now)"; }
phase BEGIN
if [ ! -f "$MARKER" ]; then
  x_state=$(/sbin/initctl status x 2>&1 || true)
  gui_state=$(/sbin/initctl status lab126_gui 2>&1 || true)
  case "$x_state:$gui_state" in
    *'start/running'*'start/running'*) echo FRAMEWORK_RESTORE_NOT_NEEDED=1; exit 0;;
  esac
  echo FRAMEWORK_RECOVERY_WITHOUT_MARKER=1
fi
echo KINDLE_DISPLAY_STACK_RESTORE_BEGIN=1

# lab126_gui stops when x stops, so bring the X display job up first.
x_status=$(/sbin/initctl status x 2>&1 || true)
case "$x_status" in
  *'start/running'*) echo KINDLE_DISPLAY_STACK_ALREADY_RUNNING=x;;
  *'stop/waiting'*)
    phase X_START_BEGIN
    if ! /sbin/start x; then
      x_status=$(/sbin/initctl status x 2>&1 || true)
      case "$x_status" in *'start/running'*) :;; *) echo KINDLE_DISPLAY_STACK_START_FAILED=1 >&2; exit 4;; esac
    fi
    ;;
  *) echo "KINDLE_DISPLAY_STACK_STATE_UNKNOWN status=$x_status" >&2; exit 4;;
esac
i=0
while [ "$i" -lt 90 ]; do
  x_status=$(/sbin/initctl status x 2>&1 || true)
  # Kindle BusyBox ps hides init jobs without a controlling terminal unless
  # full-format output is requested. The default ps falsely reports that the
  # display processes are absent when this script runs over SSH.
  if echo "$x_status" | grep -q 'start/running' && ps -ef | grep -q '[X]org' && ps -ef | grep -q '[a]wesome' && ps -ef | grep -q '[b]lanket'; then
    break
  fi
  i=$((i + 1))
  sleep 1
done
[ "$i" -lt 90 ] || { echo "KINDLE_DISPLAY_STACK_RESTORE_TIMEOUT status=$x_status marker=$MARKER" >&2; exit 5; }
echo KINDLE_DISPLAY_STACK_RESTORED=1
phase X_READY

status=$(/sbin/initctl status lab126_gui 2>&1 || true)
case "$status" in
  *'start/running'*) echo FRAMEWORK_ALREADY_RUNNING=1;;
  *'stop/waiting'*)
    phase GUI_START_BEGIN
    if ! /sbin/start lab126_gui; then
      status=$(/sbin/initctl status lab126_gui 2>&1 || true)
      case "$status" in *'start/running'*) :;; *) echo FRAMEWORK_START_COMMAND_FAILED=1 >&2; exit 2;; esac
    fi
    ;;
  *) echo "FRAMEWORK_STATE_UNKNOWN status=$status" >&2; exit 2;;
esac
i=0
while [ "$i" -lt 90 ]; do
  status=$(/sbin/initctl status lab126_gui 2>&1 || true)
  case "$status" in *'start/running'*)
    rm -f "$MARKER"
    phase GUI_READY
    phase SYNC_BEGIN
    sync
    phase SYNC_DONE
    echo KINDLE_FRAMEWORK_RESTORED=1
    exit 0;; esac
  i=$((i + 1))
  sleep 1
done
echo "KINDLE_FRAMEWORK_RESTORE_TIMEOUT status=$status marker=$MARKER" >&2
exit 3
