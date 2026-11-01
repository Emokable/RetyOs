#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
MARKER=$BASE/framework-stopped
phase(){ echo "NATIVE_RESTORE_PHASE=$1 monotonic_ns=$($BASE/session-freezer now)"; }
phase BEGIN
restore_x=1
restore_gui=1
if [ -f "$BASE/framework-state.before" ]; then
  version= x_before= gui_before=
  while IFS='=' read -r key value; do
    case "$key:$value" in
      version:1) version=1;;
      x:running|x:stopped) x_before=$value;;
      lab126_gui:running|lab126_gui:stopped) gui_before=$value;;
      *) echo "FRAMEWORK_STATE_LEDGER_INVALID key=$key value=$value" >&2; exit 8;;
    esac
  done < "$BASE/framework-state.before"
  [ "$version" = 1 ] && [ -n "$x_before" ] && [ -n "$gui_before" ] || {
    echo FRAMEWORK_STATE_LEDGER_INCOMPLETE=1 >&2; exit 8;
  }
  [ "$x_before" = running ] || restore_x=0
  [ "$gui_before" = running ] || restore_gui=0
  [ "$restore_gui" = 0 ] || [ "$restore_x" = 1 ] || {
    echo FRAMEWORK_STATE_LEDGER_DEPENDENCY_INVALID=lab126_gui_requires_x >&2; exit 8;
  }
  echo "FRAMEWORK_STATE_LEDGER_LOADED x=$x_before lab126_gui=$gui_before"
fi
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
if [ "$restore_x" = 1 ]; then case "$x_status" in
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
else
  echo KINDLE_DISPLAY_STACK_RESTORE_SKIPPED=originally_stopped
fi

status=$(/sbin/initctl status lab126_gui 2>&1 || true)
if [ "$restore_gui" = 0 ]; then
  echo KINDLE_FRAMEWORK_RESTORE_SKIPPED=originally_stopped
  rm -f "$MARKER" "$BASE/framework-state.before" "$BASE/framework-state.stopped"
  echo KINDLE_FRAMEWORK_RESTORED=1
  exit 0
fi
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
    rm -f "$BASE/framework-state.before" "$BASE/framework-state.stopped"
    echo KINDLE_FRAMEWORK_RESTORED=1
    exit 0;; esac
  i=$((i + 1))
  sleep 1
done
echo "KINDLE_FRAMEWORK_RESTORE_TIMEOUT status=$status marker=$MARKER" >&2
exit 3
