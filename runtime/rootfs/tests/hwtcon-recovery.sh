#!/bin/sh
set -eu
stage=$1; mode=$2; d="$stage/hwtcon-exec"
mkdir -p "$d"
mount -t tmpfs -o size=1m,mode=0700,nosuid,nodev,exec tmpfs "$d"
trap 'umount "$d" 2>/dev/null || :' EXIT
cp "$stage/rootfs/tests/hwtcon-frame-probe" "$d/probe"
chmod 0700 "$d/probe"
"$d/probe" "$mode"
umount "$d"
trap - EXIT
