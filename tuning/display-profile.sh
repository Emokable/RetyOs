#!/bin/sh
# Summarise the display pipeline using the profiler that already ships in
# sources/stage3/mock_gralloc.cpp (it emits HWTCON_PROFILE when
# /data/local/tmp/kindle-display-profile exists).
#
#   sh display-profile.sh                    # parse the default log
#   sh display-profile.sh /path/to/log       # parse a specific log
#   sh display-profile.sh --enable <root>    # switch the profiler on, live session
#
# --enable expects the container root of a running or retained session, i.e. the
# stage directory (/mnt/us/kindle-a7-art.XXXXXX); it only touches a file inside
# that root and never the panel.
set -u

BASE=${BASE:-/mnt/us/kindle-android}

if [ "${1:-}" = "--enable" ]; then
    root=${2:-}
    case "$root" in
        /mnt/us/kindle-a7-art.??????) ;;
        *) echo "usage: sh display-profile.sh --enable /mnt/us/kindle-a7-art.XXXXXX" >&2; exit 2;;
    esac
    dir=$root/rootfs/data/local/tmp
    [ -d "$dir" ] || { echo "no such dir: $dir" >&2; exit 3; }
    : > "$dir/kindle-display-profile"
    echo "profiler enabled: $dir/kindle-display-profile"
    echo "turn ~20 pages, then re-run without --enable and pass the session log:"
    echo "  sh display-profile.sh $root/session.log"
    exit 0
fi

LOG=${1:-$BASE/session.log}
[ -f "$LOG" ] || { echo "log not found: $LOG" >&2; exit 4; }

section() { printf '\n=== %s ===\n' "$1"; }

section "profiler samples in $LOG"
printf 'HWTCON_PROFILE lines: %s\n' "$(grep -c HWTCON_PROFILE "$LOG" 2>/dev/null || echo 0)"
printf 'display mode switches: %s (0=color 1=gray16 2=black-white)\n' \
    "$(grep -c 'HWTCON_DISPLAY_MODE=' "$LOG" 2>/dev/null || echo 0)"
printf 'fast-scroll toggles:   %s\n' "$(grep -c 'HWTCON_SCROLL_FAST=' "$LOG" 2>/dev/null || echo 0)"
printf 'hardware swipes:       %s\n' "$(grep -c 'HWTCON_DIRECTION selected' "$LOG" 2>/dev/null || echo 0)"

stats() {  # $1=label, stdin=values in us
    sort -n | awk -v label="$1" '
        { v[NR] = $1; sum += $1 }
        END {
            if (NR == 0) { printf "  %-12s <no samples>\n", label; exit }
            p50 = v[int((NR + 1) / 2)]
            p90 = v[int(NR * 0.9) < 1 ? 1 : int(NR * 0.9)]
            printf "  %-12s n=%-4d min=%7.1f p50=%7.1f p90=%7.1f max=%7.1f ms\n",
                   label, NR, v[1] / 1000, p50 / 1000, p90 / 1000, v[NR] / 1000
        }'
}

section 'per-frame cost (from HWTCON_PROFILE)'
grep HWTCON_PROFILE "$LOG" 2>/dev/null | sed -n 's/.*compare_us=\([0-9]*\).*/\1/p' | stats compare
grep HWTCON_PROFILE "$LOG" 2>/dev/null | sed -n 's/.*wait_us=\([0-9]*\).*/\1/p'    | stats wait
grep HWTCON_PROFILE "$LOG" 2>/dev/null | sed -n 's/.*total_us=\([0-9]*\).*/\1/p'   | stats total

section 'how to read this'
cat <<'EOF'
  compare  = FindDamageRgba8888 pixel diff (CPU, 1272x1696 RGBA)
  wait     = blocking wait for the previous panel update to complete
  total    = whole onFrameAvailable call, including MXCFB_SEND_UPDATE

  If `total` is dominated by `compare`, the cost is CPU and the fix is in the
  diff path (or in skipping ConvertReadingRgba8888 for unchanged rows).
  If `wait` dominates, the cost is the panel itself; shrinking the update
  region is the only lever, and banding a full-page change makes it worse
  (one big region beats N small ones with per-send overhead).

  collect a second batch with /data/local/tmp/kindle-display-animation
  containing "1" to compare the hardware swipe path against plain partial.
EOF
