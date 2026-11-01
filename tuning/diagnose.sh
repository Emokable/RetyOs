#!/bin/sh
# Read-only diagnosis for cold-boot cost. Modifies nothing; safe to run while
# a session is live or retained.
#
#   sh diagnose.sh
#
# Answers three questions that decide what to optimise next:
#   1. is /system the ext4 cache image (so boot.art/boot-*.oat are real)?
#   2. does ART boot with a preloaded image, or fall back to interpreter?
#   3. how much does PackageManager have to scan on every boot?
set -u

BASE=${BASE:-/mnt/us/kindle-android}
section() { printf '\n=== %s ===\n' "$1"; }

stage=$(sed -n '2p' "$BASE/warm-state" 2>/dev/null || :)
[ -n "$stage" ] || stage=$(cat "$BASE/boot-progress.stage" 2>/dev/null || :)
case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) stage='' ;; esac

section 'environment'
echo "kernel: $(uname -a)"
echo "memory: $(awk '/MemTotal|MemAvailable|SwapTotal/{printf "%s=%dMB ", $1, $2/1024}' /proc/meminfo 2>/dev/null)"
echo "swap:   $(grep -c . /proc/swaps 2>/dev/null || echo 0) entries"
echo "stage:  ${stage:-<none: no live or retained session>}"

section 'images and caches'
for f in payload.tar.gz system-cache.img system-cache-bootstrap.tar fixed-bootstrap.tar \
         fixed-runtime.ready system-cache.ready data.img; do
    if [ -f "$BASE/$f" ]; then
        printf '  %-30s %s bytes  mtime=%s\n' "$f" \
            "$(stat -c %s "$BASE/$f" 2>/dev/null || echo '?')" \
            "$(stat -c %y "$BASE/$f" 2>/dev/null | cut -d. -f1)"
    else
        printf '  %-30s <absent>\n' "$f"
    fi
done

[ -n "$stage" ] || {
    section 'art checks skipped'
    echo 'No session found. Start RetyOs once, keep it retained, then re-run.'
    exit 0
}

section 'is /system the cache image?'
guard=
for p in /proc/[0-9]*; do
    [ "$(cat "$p/comm" 2>/dev/null)" = art-guard ] || continue
    [ "$(readlink "$p/root" 2>/dev/null)" = "$stage/rootfs" ] || continue
    guard=$p; break
done
if [ -z "$guard" ] || [ ! -r "$guard/mountinfo" ]; then
    echo '  unknown: no accessible container mount namespace'
    exit 0
fi
sysroot=$guard/root
awk '$5=="/system" { print "  " $0; found=1 } END { if (!found) print "  no separate /system mount shown in container namespace" }' "$guard/mountinfo"

section 'art precompiled artifacts'
echo 'boot image in /system/framework/arm:'
ls -l "$sysroot/system/framework/arm/" 2>/dev/null | grep -E 'boot(\.|-)|total' | head -20 \
    || echo '  <missing>'
printf 'system odex count: %s\n' "$(find "$sysroot/system" -name '*.odex' 2>/dev/null | wc -l)"
echo '/data/dalvik-cache/arm:'
ls -l "$sysroot/data/dalvik-cache/arm/" 2>/dev/null | head -15 \
    || echo '  <empty or absent: no AOT output for installed apps>'

section 'packagemanager scan cost'
if [ -d "$sysroot/data/app" ]; then
    printf 'installed apk (data/app): %s\n' "$(find "$sysroot/data/app" -name '*.apk' 2>/dev/null | wc -l)"
else
    echo 'installed apk (data/app): unknown (data unavailable in this session state)'
fi
printf 'system apk:               %s\n' "$(find "$sysroot/system/app" "$sysroot/system/priv-app" -name '*.apk' 2>/dev/null | wc -l)"

section 'key processes'
found=0
for p in /proc/[0-9]*; do
    c=$(cat "$p/comm" 2>/dev/null || :)
    case "$c" in
        art-guard|system_server|zygote|surfaceflinger|hwtcon*)
            printf '  %-8s %-18s %s\n' "$(basename "$p")" "$c" \
                "$(awk '/^VmRSS/{print $2" "$3}' "$p/status" 2>/dev/null)"
            found=1;;
    esac
done
[ "$found" = 1 ] || echo '  <none: container not running>'

section 'boot markers'
grep -hE 'ANDROID_DIGEST_CACHE|ANDROID_BOOTSTRAP_SHA_CACHE|ANDROID_SYSTEM_CACHE_FAST_CHECK_MS|ANDROID_PAYLOAD_SHA256_MS|ANDROID_FIXED_RUNTIME_HIT|ART|dex2oat' \
    "$BASE/session.log" 2>/dev/null | tail -20
