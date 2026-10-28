#!/bin/sh
set -eu
BASE=/mnt/us/kindle-android
if [ -x "$BASE/session-controller" ] && [ -f "$BASE/session-controller.sha256" ];then
    (cd "$BASE" && sha256sum -c session-controller.sha256) || exit 8
    exec "$BASE/session-controller" run "$@"
fi
exec /bin/sh "$BASE/warm-session.legacy.sh" "$@"
