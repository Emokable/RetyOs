#!/bin/sh
# Text-only Colorsoft startup page. No images, animation or background service.
set -eu
FB=/var/local/kmc/bin/fbink
FONT=/usr/java/lib/fonts/STHeitiMedium.ttf
[ -x "$FB" ] || exit 0
case "${1:-}" in
init)
    if [ -r "$FONT" ]; then
        "$FB" -q -b -c -C BLACK -B WHITE -m -t "regular=$FONT,px=96,top=400,bottom=1100,left=90,right=90" RetyOs
        "$FB" -q -b -C GRAY8 -B WHITE -m -t "regular=$FONT,px=28,top=565,bottom=1030,left=90,right=90" '正在准备阅读空间'
        "$FB" -q -b -C GRAY8 -B WHITE -m -t "regular=$FONT,px=24,top=1220,bottom=400,left=90,right=90" '准备完成后将自动进入桌面'
        "$FB" -q -w -f -W GC16 -s
    else
        "$FB" -q -w -c -f -W GC16 -C BLACK -B WHITE -pm -S 6 -y 8 RetyOs
    fi
    ;;
progress)
    percent=${2:?};label=${3:?};elapsed=${4:?}
    case "$percent" in ''|*[!0-9]*) exit 2;; esac
    case "$elapsed" in ''|*[!0-9]*) exit 2;; esac
    [ "$percent" -ge 0 ] && [ "$percent" -le 100 ] || exit 2
    if [ -r "$FONT" ]; then
        # Batch framebuffer writes, then refresh this rectangle once with GL16.
        "$FB" -q -b -B GRAYC -k top=780,left=140,width=992,height=5
        filled=$((percent * 992 / 100))
        if [ "$filled" -gt 0 ]; then
            "$FB" -q -b -B BLACK -k "top=780,left=140,width=$filled,height=5"
        fi
        text=$(printf '%s%%  ·  %s\n\n已用时 %s 秒' "$percent" "$label" "$elapsed")
        "$FB" -q -b -C BLACK -B WHITE -m -t "regular=$FONT,px=30,top=870,bottom=600,left=90,right=90,padding=BOTH" "$text"
        "$FB" -q -w -W GL16 -s top=760,left=90,width=1092,height=360
    else
        "$FB" -q -w -W GL16 -C BLACK -B WHITE -pm -y 14 "RetyOs  $percent%  |  ${elapsed}s"
    fi
    ;;
*) exit 2;;
esac
