#!/bin/sh
# Resolve kernel loop/zram devices across Kindle devtmpfs/udev naming changes.
set -eu
ensure_node(){
 kind=$1;dev=$2
 case "$kind" in loop|zram) :;; *) exit 2;;esac
 number=${dev#/dev/$kind}
 case "$number" in ''|*[!0-9]*) echo "ANDROID_BLOCK_NODE_INVALID=$dev" >&2;exit 2;;esac
 [ "$dev" = "/dev/$kind$number" ] || exit 2
 sysdev=/sys/block/$kind$number/dev
 [ -r "$sysdev" ] || { echo "ANDROID_BLOCK_SYSFS_MISSING=$sysdev" >&2;exit 3; }
 numbers=$(cat "$sysdev")
 major=${numbers%:*};minor=${numbers#*:}
 case "$major:$minor" in *[!0-9:]*|:*) exit 3;;esac
 [ -n "$major" ] && [ -n "$minor" ] || exit 3
 [ "$minor" -eq "$number" ] || exit 3
 if [ "$kind" = loop ];then [ "$major" -eq 7 ] || exit 3;fi
 expected=$(printf '%x:%x' "$major" "$minor")
 if [ ! -e "$dev" ] && [ ! -L "$dev" ];then
  if mknod -m 0600 "$dev" b "$major" "$minor";then
   echo "ANDROID_BLOCK_NODE_CREATED=$dev major=$major minor=$minor" >&2
  fi
 fi
 [ -b "$dev" ] && [ "$(stat -Lc '%t:%T' "$dev")" = "$expected" ] || {
  echo "ANDROID_BLOCK_NODE_MISMATCH=$dev expected=$expected" >&2;exit 4;
 }
 printf '%s\n' "$dev"
}
case "${1:-loop-free}" in
 loop-free)
  [ "$#" -le 1 ] || exit 2
  dev=$(losetup -f)
  ensure_node loop "$dev"
  ;;
 loop-device)
  [ "$#" = 2 ] || exit 2
  ensure_node loop "$2"
  ;;
 zram)
  [ "$#" = 2 ] || exit 2
  ensure_node zram "$2"
  ;;
 *) exit 2;;
esac
