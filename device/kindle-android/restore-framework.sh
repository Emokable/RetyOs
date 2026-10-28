#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
if [ -f "$BASE/native-ui.lease" ];then
    "$BASE/session-controller" native-release
fi
exec /bin/sh "$BASE/restore-framework.legacy.sh" "$@"
