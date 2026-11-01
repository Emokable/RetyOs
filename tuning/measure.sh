#!/bin/sh
# Read-only boot-timing report. Does not modify anything.
#
#   sh measure.sh            # default BASE=/mnt/us/kindle-android
#
# Prints one row per ANDROID_BOOT_STAGE so the cold boot can be split into
# segments, plus the tuning/cache markers emitted by start.sh.
set -u

BASE=${BASE:-/mnt/us/kindle-android}

section() { printf '\n=== %s ===\n' "$1"; }

section 'boot stage segments (seconds since the first stage)'
printf '%6s %9s %9s  %s\n' 'stage' 'segment' 'total' 'label'

cat "$BASE/entry.log" "$BASE/session.log" 2>/dev/null | awk '
/ANDROID_BOOT_STAGE=/ {
    pct = ""; lab = ""; mono = ""
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^ANDROID_BOOT_STAGE=/) { split($i, a, "="); pct = a[2] }
        else if ($i ~ /^label=/)         { split($i, a, "="); lab = a[2] }
        else if ($i ~ /^monotonic=/)     { split($i, a, "="); mono = a[2] }
    }
    if (pct == "" || mono == "") next
    if (prev != "" && mono + 0 < prev + 0) { prev = ""; base = "" }
    if (base == "") base = mono
    seg = (prev == "" ? 0 : mono - prev)
    printf "%5s%% %9.2f %9.2f  %s\n", pct, seg, mono - base, lab
    prev = mono
}'

section 'tuning and cache markers'
grep -hE 'ANDROID_DIGEST_CACHE|ANDROID_BOOTSTRAP_SHA_CACHE|ANDROID_SYSTEM_CACHE_FAST_CHECK_MS|ANDROID_PAYLOAD_SHA256_MS|ANDROID_FIXED_RUNTIME_HIT|ANDROID_COLD_SOFTWARE_READY|BOOT_PROGRESS_READY|FULL_EXIT_SOFTWARE_READY' \
    "$BASE/session.log" "$BASE/entry.log" 2>/dev/null | tail -20

section 'session result'
grep -hE 'APK_(FRAMEWORK_SERVICES_READY|PM_INSTALL|ACTIVITY_PROCESS)|ANDROID_SESSION_EXITED|SESSION_FINAL_STATUS' \
    "$BASE/session.log" 2>/dev/null | tail -12

section 'warm-session state'
printf 'warm-state:        %s\n' "$(head -n 1 "$BASE/warm-state" 2>/dev/null || echo '<none>')"
printf 'warm resume reqs:  %s\n' "$(grep -c ANDROID_ENTRY_WARM_RESUME_REQUESTED "$BASE/entry.log" 2>/dev/null || :)"

section 'bootstrap verification'
echo 'The launcher caches large-file digests by size and second-resolution mtime.'
