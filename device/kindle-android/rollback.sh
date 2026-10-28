#!/bin/sh
set -eu

BASE=/mnt/us/kindle-android
BACKUP=$BASE/rollback-current
DOCS=/mnt/us/documents
[ -d /mnt/base-us/documents ] && DOCS=/mnt/base-us/documents

[ ! -e "$BASE/session.lock" ] || { echo ANDROID_ROLLBACK_REFUSED=session_running; exit 2; }
[ ! -e "$BASE/framework-stopped" ] || { echo ANDROID_ROLLBACK_REFUSED=framework_recovery_pending; exit 3; }
[ "$(cat "$BACKUP/backup-marker" 2>/dev/null || :)" = KINDLE_ANDROID_ROLLBACK_BACKUP=1 ] || { echo ANDROID_ROLLBACK_REFUSED=backup_marker_invalid; exit 7; }
[ -d "$BACKUP/kindle-android" ] || { echo ANDROID_ROLLBACK_REFUSED=backup_missing; exit 4; }
[ -f "$BACKUP/kindle-android/payload.tar.gz" ] || { echo ANDROID_ROLLBACK_REFUSED=payload_missing; exit 5; }
expected=$(awk '{print $1}' "$BACKUP/kindle-android/payload.sha256")
actual=$(sha256sum "$BACKUP/kindle-android/payload.tar.gz" | awk '{print $1}')
[ "$actual" = "$expected" ] || { echo ANDROID_ROLLBACK_REFUSED=backup_hash_mismatch; exit 6; }

restore_base_file() {
    name=$1
    source=$BACKUP/kindle-android/$name
    destination=$BASE/$name
    if [ -f "$source" ]; then cp -p "$source" "$destination.rollback-new"; else rm -f "$destination"; fi
}

for name in payload.tar.gz payload.sha256 start.sh recover.sh restore-framework.sh \
            uninstall.sh prepare-data.sh network-relay.sh slirp4netns \
            slirp4netns-SHA256SUMS THIRD_PARTY_NOTICES.md rollback.sh; do
    restore_base_file "$name"
done

mkdir -p "$BASE/licenses"
if [ -f "$BACKUP/kindle-android/licenses/slirp4netns-COPYING" ]; then
    cp -p "$BACKUP/kindle-android/licenses/slirp4netns-COPYING" "$BASE/licenses/.slirp4netns-COPYING.rollback-new"
else
    rm -f "$BASE/licenses/slirp4netns-COPYING"
fi

source=$BACKUP/documents/start-android.sh
if [ -f "$source" ]; then cp -p "$source" "$DOCS/.start-android.sh.rollback-new"; else rm -f "$DOCS/start-android.sh"; fi

sync
for name in payload.tar.gz payload.sha256 start.sh recover.sh restore-framework.sh \
            uninstall.sh prepare-data.sh network-relay.sh slirp4netns \
            slirp4netns-SHA256SUMS THIRD_PARTY_NOTICES.md rollback.sh; do
    destination=$BASE/$name
    [ ! -f "$destination.rollback-new" ] || mv "$destination.rollback-new" "$destination"
done
[ ! -f "$BASE/licenses/.slirp4netns-COPYING.rollback-new" ] || mv "$BASE/licenses/.slirp4netns-COPYING.rollback-new" "$BASE/licenses/slirp4netns-COPYING"
[ ! -f "$DOCS/.start-android.sh.rollback-new" ] || mv "$DOCS/.start-android.sh.rollback-new" "$DOCS/start-android.sh"
chmod 0755 "$BASE/start.sh" "$BASE/recover.sh" "$BASE/restore-framework.sh" \
    "$BASE/uninstall.sh" "$BASE/prepare-data.sh" "$DOCS/start-android.sh"
[ ! -x "$BASE/network-relay.sh" ] || chmod 0755 "$BASE/network-relay.sh"
[ ! -x "$BASE/slirp4netns" ] || chmod 0755 "$BASE/slirp4netns"
sync
echo ANDROID_ROLLBACK_PASS=1
echo ANDROID_ROLLBACK_BACKUP=$BACKUP
