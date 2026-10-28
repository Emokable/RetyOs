#!/bin/sh
# Offline cache-only refresh. A failed refresh leaves cache disabled; data.img
# and the verified full-payload fallback are never modified.
set -eu
BASE=/mnt/us/kindle-android
DEVICE_HELPER=$BASE/kernel-device-nodes.sh
if [ "${1:-}" != --private ]; then
    exec unshare -m --propagation private sh "$0" --private
fi
[ ! -e "$BASE/session.lock" ] || exit 2
[ -x "$DEVICE_HELPER" ] || { echo SYSTEM_CACHE_REFRESH_ERROR=kernel_device_helper_missing; exit 2; }
for proc in /proc/[0-9]*; do
    [ "$(cat "$proc/comm" 2>/dev/null)" != art-guard ] || exit 3
done
[ "$(readlink -f "$BASE/system-cache.img")" = "$BASE/system-cache.img" ] || exit 4
if losetup -a | grep -F 'system-cache.img'; then exit 5; fi
ready=$BASE/system-cache.ready
[ -f "$ready" ] || exit 6
payload=$(awk '{print $1}' "$BASE/payload.sha256")
[ "$(sha256sum "$BASE/system-cache.img" | awk '{print $1}')" = "$(sed -n '2p' "$ready")" ] || exit 8
(cd "$BASE" && sha256sum -c app-graphics-overlay.tar.gz.sha256 && sha256sum -c gralloc.hwtcon.so.sha256)
work=$(mktemp -d "$BASE/system-cache-refresh.XXXXXX")
loop=
mounted=0
cleanup() {
    if [ "$mounted" = 1 ]; then umount "$work/mount" || return; fi
    if [ -n "$loop" ]; then losetup -d "$loop" || return; fi
    case "$work" in "$BASE"/system-cache-refresh.??????)
        [ "$(readlink -f "$work")" = "$work" ] || return
        rm -rf -- "$work";;
    esac
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
mv "$ready" "$BASE/system-cache.ready.previous"
mkdir "$work/overlay" "$work/mount"
tar -xzf "$BASE/app-graphics-overlay.tar.gz" --no-same-owner -C "$work/overlay"
loop=$("$DEVICE_HELPER")
losetup "$loop" "$BASE/system-cache.img"
mount -t ext4 -o rw,nosuid,nodev "$loop" "$work/mount"
mounted=1
cp -a "$work/overlay/rootfs/system/." "$work/mount/"
rm -rf "$work/mount/app/WeRead"
cp "$BASE/gralloc.hwtcon.so" "$work/mount/lib/hw/gralloc.default.so"
key=$(cat "$BASE/payload.sha256" "$BASE/app-graphics-overlay.tar.gz.sha256" "$BASE/gralloc.hwtcon.so.sha256" | sha256sum | awk '{print $1}')
printf '%s\n' "$key" > "$work/mount/.kindle-system-cache-key"
sync
umount "$work/mount"
mounted=0
losetup -d "$loop"
loop=
image_sha=$(sha256sum "$BASE/system-cache.img" | awk '{print $1}')
bootstrap_sha=$(sha256sum "$BASE/system-cache-bootstrap.tar" | awk '{print $1}')
size=$(stat -c %s "$BASE/system-cache.img")
printf '%s\n%s\n%s\n%s\n%s\n' "$key" "$image_sha" "$bootstrap_sha" "$size" "$payload" > "$ready.new"
mv "$ready.new" "$ready"
echo "SYSTEM_CACHE_REFRESH_PASS key=$key"
