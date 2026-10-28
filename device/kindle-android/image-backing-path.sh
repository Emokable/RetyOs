#!/bin/sh
# Select the existing native file beneath Kindle's MTP/FUSE view.
# Never change an attached loop device or create/migrate an image here.
set -eu
B=/mnt/us/kindle-android
N=/mnt/base-us/kindle-android
case "${1:-}" in data.img|system-cache.img) name=$1;; *) exit 2;; esac
visible=$B/$name
native=$N/$name
fallback(){ echo "$visible"; exit 0; }
[ ! -e "$B/native-image-path.disabled" ] || fallback
awk '$2=="/mnt/base-us" && $3=="ext4" {ok=1} END {exit !ok}' /proc/mounts || fallback
awk '$2=="/mnt/us" && $3=="fuse.fsp" {ok=1} END {exit !ok}' /proc/mounts || fallback
[ -f "$native" ] && [ ! -L "$native" ] && [ -f "$visible" ] && [ ! -L "$visible" ] || fallback
[ "$(readlink -f "$native")" = "$native" ] || fallback
[ "$(readlink -f "$visible")" = "$visible" ] || fallback
size=$(stat -c %s "$visible")
[ "$size" = "$(stat -c %s "$native")" ] || fallback
[ "$size" -ge 8192 ] || fallback
# Identity guard for a known mirror, not an image-integrity replacement.
# Images are detached when the launcher calls this helper.
last=$((size/4096-1))
for block in 0 "$last";do
 a=$(dd if="$visible" bs=4096 skip="$block" count=1 2>/dev/null | sha256sum)
 b=$(dd if="$native" bs=4096 skip="$block" count=1 2>/dev/null | sha256sum)
 [ "$a" = "$b" ] || fallback
done
echo "ANDROID_IMAGE_BACKING=$native" >&2
echo "$native"
