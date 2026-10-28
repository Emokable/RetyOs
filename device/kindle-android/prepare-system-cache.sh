#!/bin/sh
# Optional, rebuildable read-only system cache. Application data is never copied.
set -eu
BASE=/mnt/us/kindle-android
DEVICE_HELPER=$BASE/kernel-device-nodes.sh
if [ "${1:-}" = --from-stage ]; then
    exec unshare -m --propagation private sh "$0" --private-stage "${2:?stage required}"
fi
if [ "${1:-}" != --private ] && [ "${1:-}" != --private-stage ]; then
    exec unshare -m --propagation private sh "$0" --private "${1:?active stage required}"
fi
stage=${2:?}
case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) exit 2;; esac
[ "$(readlink -f "$stage")" = "$stage" ] || exit 2
[ -x "$DEVICE_HELPER" ] || { echo SYSTEM_CACHE_ERROR=kernel_device_helper_missing; exit 2; }
source=
if [ "${1:-}" = --private-stage ]; then
    [ -d "$BASE/session.lock" ] || exit 3
    [ -d "$stage/rootfs/system" ] && [ ! -L "$stage/rootfs/system" ] || exit 3
    source=$stage/rootfs/system
else
for proc in /proc/[0-9]*; do
    [ "$(cat "$proc/comm" 2>/dev/null)" = art-guard ] || continue
    [ "$(readlink "$proc/root" 2>/dev/null)" = "$stage/rootfs" ] || continue
    source=$proc/root/system
    break
done
fi
[ -n "$source" ] || { echo SYSTEM_CACHE_ERROR=no_active_source; exit 3; }
if [ "${1:-}" != --private-stage ]; then
    [ ! -e "$BASE/system-cache.img" ] || { echo SYSTEM_CACHE_ERROR=cache_already_exists; exit 4; }
fi
for backing in /sys/block/loop*/loop/backing_file; do
    [ -r "$backing" ] || continue
    case "$(cat "$backing")" in
        */kindle-android/system-cache.img) echo SYSTEM_CACHE_ERROR=cache_image_attached; exit 4;;
    esac
done
key=$(cat "$BASE/payload.sha256" "$BASE/app-graphics-overlay.tar.gz.sha256" "$BASE/gralloc.hwtcon.so.sha256" | sha256sum | awk '{print $1}')
needed=$(du -sk "$source" | awk '{print $1}')
size=$(( (needed + needed / 8 + 65536 + 16383) / 16384 * 16384 ))
free=$(df -Pk /mnt/us | tail -n 1 | awk '{print $4}')
[ "$free" -gt $((size + 200000)) ] || { echo SYSTEM_CACHE_ERROR=insufficient_space; exit 5; }
work=$(mktemp -d "$BASE/system-cache-build.XXXXXX")
loop=
mounted=0
cleanup() {
    if [ "$mounted" = 1 ]; then umount "$work/mount" || return; fi
    if [ -n "$loop" ]; then losetup -d "$loop" || return; fi
    case "$work" in "$BASE"/system-cache-build.??????)
        [ "$(readlink -f "$work")" = "$work" ] || return
        rm -rf -- "$work";;
    esac
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
echo "SYSTEM_CACHE_BUILD_BEGIN size_kib=$size source_kib=$needed"
truncate -s $((size * 1024)) "$work/system.img"
mkfs.ext4 -F -m 0 "$work/system.img" >/dev/null
loop=$("$DEVICE_HELPER")
losetup "$loop" "$work/system.img"
mkdir "$work/mount"
mount -t ext4 -o rw,nosuid,nodev "$loop" "$work/mount"
mounted=1
cp -a "$source/." "$work/mount/"
rm -rf "$work/mount/app/WeRead"
mkdir -p "$work/mount/app" "$work/mount/priv-app" "$work/mount/vendor/app"
printf '%s\n' "$key" > "$work/mount/.kindle-system-cache-key"
sync
umount "$work/mount"
mounted=0
losetup -d "$loop"
loop=
mkdir "$work/bootstrap"
tar -xzf "$BASE/payload.tar.gz" --no-same-owner --exclude=rootfs/system --exclude='rootfs/system/*' -C "$work/bootstrap"
mkdir -p "$work/bootstrap/rootfs/system"
tar -cf "$work/bootstrap.tar" -C "$work/bootstrap" .
bootstrap_sha=$(sha256sum "$work/bootstrap.tar" | awk '{print $1}')
image_sha=$(sha256sum "$work/system.img" | awk '{print $1}')
mv "$work/system.img" "$BASE/system-cache.img"
mv "$work/bootstrap.tar" "$BASE/system-cache-bootstrap.tar"
printf '%s\n%s\n%s\n%s\n%s\n' "$key" "$image_sha" "$bootstrap_sha" "$((size * 1024))" "$(awk '{print $1}' "$BASE/payload.sha256")" > "$BASE/system-cache.ready.new"
mv "$BASE/system-cache.ready.new" "$BASE/system-cache.ready"
echo "SYSTEM_CACHE_BUILD_PASS key=$key size_kib=$size"
