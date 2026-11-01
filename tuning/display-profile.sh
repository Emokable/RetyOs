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
    dir=
    for proc in /proc/[0-9]*; do
        [ "$(cat "$proc/comm" 2>/dev/null)" = art-guard ] || continue
        [ "$(readlink "$proc/root" 2>/dev/null)" = "$root/rootfs" ] || continue
        [ -d "$proc/root/data/local/tmp" ] || continue
        dir=$proc/root/data/local/tmp
        break
    done
    [ -n "$dir" ] || { echo "no active container data namespace for $root" >&2; exit 3; }
    : > "$dir/kindle-display-profile"
    echo "profiler enabled: $dir/kindle-display-profile"
    echo "turn ~20 pages, then re-run without --enable and pass the session log:"
    echo "  sh display-profile.sh $root/result.txt"
    exit 0
fi

LOG=${1:-$BASE/session.log}
[ -f "$LOG" ] || { echo "log not found: $LOG" >&2; exit 4; }

section() { printf '\n=== %s ===\n' "$1"; }

section "profiler samples in $LOG"
count() { grep -c "$1" "$LOG" 2>/dev/null || :; }
printf 'HWTCON_PROFILE lines: %s\n' "$(count HWTCON_PROFILE)"
printf 'display mode switches: %s (0=color 1=gray16 2=black-white)\n' \
    "$(count 'HWTCON_DISPLAY_MODE=')"
printf 'fast-scroll toggles:   %s\n' "$(count 'HWTCON_SCROLL_FAST=')"
printf 'hardware swipes:       %s\n' "$(count 'HWTCON_DIRECTION selected')"

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
field() { awk -v key="$1" '/HWTCON_PROFILE/ { for(i=1;i<=NF;i++) { split($i,a,"="); if(a[1]==key && a[2] ~ /^[0-9]+$/) print a[2] } }' "$LOG"; }
field compare_us | stats compare
field previous_panel_wait_us | stats previous_wait
field wait_submission_us | stats submit_wait
field wait_update_complete_ioctl_us | stats complete_ioctl
field post_total_us | stats post_total

section 'how to read this'
cat <<'EOF'
  compare  = FindDamageRgba8888 pixel diff (CPU, 1272x1696 RGBA)
  previous_wait = waiting for a previous update; submit_wait = submission wait
  complete_ioctl = completion ioctl duration; post_total = software post duration
  An ioctl return alone does not establish physical panel completion.

  collect a second batch with /data/local/tmp/kindle-display-animation
  containing "1" to compare the hardware swipe path against plain partial.
EOF
