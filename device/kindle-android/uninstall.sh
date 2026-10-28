#!/bin/sh
set -eu

BASE=/mnt/us/kindle-android
DOCS=/mnt/us/documents
[ -d /mnt/base-us/documents ] && DOCS=/mnt/base-us/documents
ENTRY=$DOCS/start-android.sh
if [ -e "$BASE/framework-stopped" ]; then
    echo "ANDROID_UNINSTALL_REFUSED=framework_recovery_pending; run $BASE/recover.sh"
    exit 5
fi
if [ -d "$BASE/session.lock" ]; then
    echo "ANDROID_UNINSTALL_REFUSED=session_running"
    exit 2
fi
for stage in /mnt/us/kindle-a7-art.*; do
    [ -d "$stage" ] || continue
    if [ -f "$stage/hwtcon-backup-ready" ]; then
        echo "ANDROID_UNINSTALL_REFUSED=recovery_pending stage=$stage"
        exit 3
    fi
done
if [ -e "$ENTRY" ] && ! grep -q 'KINDLE_ANDROID_DOCUMENT_ENTRY=1' "$ENTRY"; then
    echo "ANDROID_UNINSTALL_REFUSED=foreign_documents_entry"
    exit 4
fi
rm -f -- "$BASE/payload.tar.gz" "$BASE/payload.sha256" \
    "$BASE/start.sh" "$BASE/recover.sh" "$BASE/restore-framework.sh" "$BASE/prepare-data.sh" \
    "$BASE/network-relay.sh" "$BASE/rollback.sh" "$BASE/slirp4netns" "$BASE/slirp4netns-SHA256SUMS" \
    "$BASE/THIRD_PARTY_NOTICES.md" "$BASE/licenses/slirp4netns-COPYING" \
    "$BASE/session.log" "$BASE/session-result.tar" "$BASE/network-relay.log" \
    "$BASE/framework-watchdog.log" "$BASE/framework-watchdog.pid"
[ -d "$BASE/licenses" ] && rmdir "$BASE/licenses" 2>/dev/null || :
if [ -e "$BASE/rollback-current" ]; then
    if [ "$(cat "$BASE/rollback-current/backup-marker" 2>/dev/null || :)" = KINDLE_ANDROID_ROLLBACK_BACKUP=1 ]; then
        rm -rf -- "$BASE/rollback-current"
    else
        echo ANDROID_UNINSTALL_PRESERVED_FOREIGN_ROLLBACK_PATH=1
    fi
fi
if [ -e "$ENTRY" ]; then rm -f -- "$ENTRY"; fi
if [ -f "$BASE/data.img" ]; then
    echo "ANDROID_ENTRY_REMOVED=1 DATA_IMAGE_PRESERVED=$BASE/data.img"
else
    rmdir "$BASE" 2>/dev/null || :
    echo "ANDROID_ENTRY_REMOVED=1"
fi
