#!/bin/sh
set -eu

BASE=/mnt/us/kindle-android
IMAGE=$BASE/data.img
MOUNT=$BASE/.data-init-mnt
DEVICE_HELPER=$BASE/kernel-device-nodes.sh
IMAGE_BYTES=1610612736
LEGACY_IMAGE_BYTES=1073741824

awk '$2=="/" {if ($4 !~ /(^|,)ro(,|$)/) exit 1; seen=1} END {if (!seen) exit 2}' /proc/mounts
[ -x "$DEVICE_HELPER" ] || { echo ANDROID_DATA_ERROR=kernel_device_helper_missing; exit 15; }
test ! -e "$BASE/session.lock" || { echo ANDROID_DATA_ERROR=session_running; exit 10; }
if [ -e "$IMAGE" ]; then
    [ -f "$IMAGE" ] || { echo ANDROID_DATA_ERROR=image_path_not_regular; exit 11; }
    size=$(stat -c '%s' "$IMAGE")
    if [ "$size" -eq "$LEGACY_IMAGE_BYTES" ]; then
        echo ANDROID_DATA_IMAGE_LEGACY_SIZE=1 size_bytes=$size resize_required=1
        exit 0
    fi
    [ "$size" -eq "$IMAGE_BYTES" ] || { echo ANDROID_DATA_ERROR=existing_image_wrong_size size=$size expected=$IMAGE_BYTES; exit 12; }
    echo ANDROID_DATA_IMAGE_READY=1 size_bytes=$size existing=1
    exit 0
fi

available=$(df -Pk /mnt/us | tail -n 1 | awk '{print $4}')
[ "$available" -ge 1600000 ] || { echo ANDROID_DATA_ERROR=insufficient_storage available_kib=$available required_kib=1600000; exit 13; }
[ ! -e "$MOUNT" ] || { echo ANDROID_DATA_ERROR=temporary_mount_path_exists; exit 14; }

umask 077
temporary=$(mktemp "$BASE/.data-image.XXXXXX")
loop=
mounted=0
cleanup() {
    if [ "$mounted" = 1 ]; then umount "$MOUNT" 2>/dev/null || :; fi
    if [ -n "$loop" ]; then losetup -d "$loop" 2>/dev/null || :; fi
    rmdir "$MOUNT" 2>/dev/null || :
    [ -z "$temporary" ] || rm -f "$temporary"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

truncate -s "$IMAGE_BYTES" "$temporary"
mkfs.ext4 -F -m 0 "$temporary" >/dev/null
loop=$("$DEVICE_HELPER")
losetup "$loop" "$temporary"
mkdir "$MOUNT"
mount -t ext4 -o rw,nosuid,nodev "$loop" "$MOUNT"
mounted=1
mkdir -p "$MOUNT/local/tmp" "$MOUNT/app" "$MOUNT/app-private" \
    "$MOUNT/dalvik-cache/arm" "$MOUNT/system" "$MOUNT/system/users/0" \
    "$MOUNT/system_ce/0" "$MOUNT/system_de/0" "$MOUNT/misc/user/0" \
    "$MOUNT/misc/zoneinfo/current" "$MOUNT/user/0" "$MOUNT/user_de/0"
printf 'KINDLE_ANDROID_DATA_IMAGE_V1\n' > "$MOUNT/.kindle-android-data"
sync
grep -q '^KINDLE_ANDROID_DATA_IMAGE_V1$' "$MOUNT/.kindle-android-data"
umount "$MOUNT"
mounted=0
losetup -d "$loop"
loop=
mv "$temporary" "$IMAGE"
temporary=
trap - EXIT HUP INT TERM
rmdir "$MOUNT"
size=$(stat -c '%s' "$IMAGE")
echo ANDROID_DATA_IMAGE_CREATED=1 size_bytes=$size fs=ext4 persistent=1
