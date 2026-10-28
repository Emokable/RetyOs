#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
[ -d "$BASE" ] && [ ! -L "$BASE" ] || exit 2
[ ! -e "$BASE/session.lock" ] || { echo ANDROID_INSTALL_ERROR=fully_stop_android_first; exit 3; }
cd "$BASE"
for name in payload app-graphics-overlay.tar.gz gralloc.hwtcon.so session-freezer session-power-guard session-preflight usb-session-guard native-frame-probe native-frame-state session-controller; do
    [ -f "$name.sha256" ] || exit 4
    sha256sum -c "$name.sha256" || exit 5
done
chmod 755 ./*.sh session-freezer session-power-guard session-preflight usb-session-guard native-frame-probe native-frame-state session-controller slirp4netns
sh "$BASE/prepare-data.sh"
echo ANDROID_INSTALL_READY=1
echo 'Start Android from documents/start-android.sh'
