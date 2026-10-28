#!/bin/sh
# Stop native MTP while Android runs. In the frozen native state, a private
# mount namespace exports /mnt/us but hides the complete kindle-android
# control/image directory from the host computer.
set -eu
BASE=/mnt/us/kindle-android
STATE=$BASE/mtp-held
HELPER=$BASE/frozen-mtp-export.sh

is_android_frozen() {
 case "$(head -n 1 "$BASE/warm-state" 2>/dev/null || :)" in native|native_pending) :;; *) return 1;; esac
 guard=$(sed -n '4p' "$BASE/warm-state" 2>/dev/null || :)
 case "$guard" in ''|*[!0-9]*) return 1;; esac
 [ "$(cat /sys/fs/cgroup/kindle-android-warm/session/freezer.state 2>/dev/null || :)" = FROZEN ] || return 1
 for image in data.img system-cache.img; do
  found=0
  for f in /sys/block/loop*/loop/backing_file; do
   [ -r "$f" ] || continue
   case "$(cat "$f" 2>/dev/null || :)" in /mnt/base-us/kindle-android/$image) found=1;; esac
  done
  [ "$found" = 1 ] || return 1
 done
}

stop_native_mtp() {
 status=$(/sbin/initctl status mtp 2>&1)
 case "$status" in *start/running*) /sbin/stop mtp;; *stop/waiting*) :;; *) return 1;; esac
 n=0
 while [ "$n" -lt 50 ]; do
  case "$(/sbin/initctl status mtp 2>&1)" in *stop/waiting*) return 0;; esac
  n=$((n+1)); usleep 100000
 done
 return 1
}

usb_disconnected() {
 [ ! -x "$BASE/usb-session-guard" ] || "$BASE/usb-session-guard" --check
}

case "${1:-}" in
hold)
 usb_disconnected || { echo MTP_GATE_DEFERRED_USB_CONNECTED=1; exit 15; }
 "$HELPER" stop || { echo MTP_GATE_ERROR=frozen_export_stop_failed; exit 2; }
 created_state=0
 if [ ! -f "$STATE" ]; then
  status=$(/sbin/initctl status mtp 2>&1)
  case "$status" in *start/running*) printf 'running\n' > "$STATE"; created_state=1;; *stop/waiting*) printf 'stopped\n' > "$STATE"; created_state=1;; *) echo MTP_GATE_ERROR=unknown_job_state;exit 3;; esac
 fi
 if ! usb_disconnected; then
  [ "$created_state" = 0 ] || rm -f "$STATE"
  echo MTP_GATE_DEFERRED_USB_CONNECTED=1
  exit 15
 fi
 stop_native_mtp || { echo MTP_GATE_ERROR=stop_timeout; exit 4; }
 echo ANDROID_MTP_EXPORT_HELD=1;;
open-frozen)
 is_android_frozen || { echo MTP_FROZEN_OPEN_REFUSED=state_not_frozen; exit 9; }
 [ -f "$STATE" ] || { echo MTP_FROZEN_OPEN_REFUSED=gate_not_held; exit 10; }
 stop_native_mtp || { echo MTP_FROZEN_OPEN_REFUSED=native_mtp_not_stopped; exit 11; }
 "$HELPER" start
 echo ANDROID_MTP_FROZEN_SAFE=1;;
close-frozen)
 usb_disconnected || { echo MTP_FROZEN_CLOSE_DEFERRED_USB_CONNECTED=1; exit 24; }
 "$HELPER" stop || { echo MTP_FROZEN_STOP_TIMEOUT=1; exit 18; }
 stop_native_mtp || { echo MTP_FROZEN_NATIVE_MTP_STOP_TIMEOUT=1; exit 19; }
 echo ANDROID_MTP_FROZEN_CLOSED=1;;
release)
 usb_disconnected || { echo MTP_RELEASE_DEFERRED_USB_CONNECTED=1; exit 25; }
 for proc in /proc/[0-9]*; do
  [ "$(cat "$proc/comm" 2>/dev/null || :)" = art-guard ] || continue
  case "$(readlink "$proc/root" 2>/dev/null || :)" in /mnt/us/kindle-a7-art.??????/rootfs) echo MTP_GATE_DEFERRED=android_alive;exit 5;; esac
 done
 for f in /sys/block/loop*/loop/backing_file; do
  [ -r "$f" ] || continue
  case "$(cat "$f")" in *kindle-android/data.img|*kindle-android/system-cache.img) echo MTP_GATE_DEFERRED=project_image_attached;exit 6;; esac
 done
 "$HELPER" stop || { echo MTP_RELEASE_FROZEN_EXPORT_STOP_FAILED=1; exit 20; }
 stop_native_mtp || { echo MTP_RELEASE_NATIVE_STOP_FAILED=1; exit 21; }
 [ -f "$STATE" ] || exit 0
 if [ "$(cat "$STATE")" = running ]; then
  case "$(/sbin/initctl status mtp 2>&1)" in *start/running*) :;; *stop/waiting*) /sbin/start mtp;; *) exit 7;; esac
 fi
 rm -f "$STATE"
 echo ANDROID_MTP_EXPORT_RESTORED=1;;
*) exit 2;;
esac
