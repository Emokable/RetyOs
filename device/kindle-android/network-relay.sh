#!/bin/sh
set -eu

BASE=/mnt/us/kindle-android
BIN=$BASE/slirp4netns
LOG=$BASE/network-relay.log
LOCK=$BASE/session.lock/pid
stage=${1:-}

case "$stage" in /mnt/us/kindle-a7-art.??????) ;; *) echo NETWORK_RELAY_REFUSED=invalid_stage; exit 2;; esac
[ -x "$BIN" ] || { echo NETWORK_RELAY_REFUSED=missing_binary; exit 3; }
[ -r "$LOCK" ] || { echo NETWORK_RELAY_REFUSED=session_missing; exit 4; }
launcher=$(cat "$LOCK")
case "$launcher" in ''|*[!0-9]*) echo NETWORK_RELAY_REFUSED=invalid_launcher_pid; exit 5;; esac

relay_pid=
resolver_file=
cleanup() {
    trap - EXIT HUP INT TERM
    if [ -n "$relay_pid" ]; then
        kill "$relay_pid" 2>/dev/null || :
        wait "$relay_pid" 2>/dev/null || :
    fi
    [ -z "$resolver_file" ] || rm -f "$resolver_file"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

echo NETWORK_RELAY_WAITING_FOR_ANDROID=1 >> "$LOG"
target=
i=0
while [ "$i" -lt 180 ]; do
    [ -r "$LOCK" ] || { echo NETWORK_RELAY_END=session_lock_missing >> "$LOG"; exit 0; }
    current=$(cat "$LOCK" 2>/dev/null || :)
    [ "$current" = "$launcher" ] || { echo NETWORK_RELAY_END=launcher_changed >> "$LOG"; exit 0; }
    kill -0 "$launcher" 2>/dev/null || { echo NETWORK_RELAY_END=launcher_gone >> "$LOG"; exit 0; }
    for entry in /proc/[0-9]*; do
        [ -r "$entry/comm" ] || continue
        IFS= read -r name < "$entry/comm" || :
        if [ "$name" = art-guard ]; then
            candidate=${entry#/proc/}
            candidate_root=$(readlink "$entry/root" 2>/dev/null || :)
            if [ "$candidate_root" = "$stage/rootfs" ] && kill -0 "$candidate" 2>/dev/null; then
                target=$candidate
                break
            fi
        fi
    done
    [ -n "$target" ] && break
    i=$((i + 1))
    sleep 1
done
[ -n "$target" ] || { echo NETWORK_RELAY_END=android_process_not_found >> "$LOG"; exit 6; }

# Stopping the native GUI can leave the frontlight hardware dark even while
# powerd still reports its previous values. Reapply those exact values once
# Android is ready; this preserves the user's brightness and warmth settings.
brightness=$(lipc-get-prop com.lab126.powerd flIntensity 2>/dev/null || echo invalid)
warmth=$(lipc-get-prop com.lab126.powerd currentAmberLevel 2>/dev/null || echo invalid)
case "$brightness" in ''|*[!0-9]*) brightness=invalid;; esac
case "$warmth" in ''|*[!0-9]*) warmth=invalid;; esac
if [ "$brightness" != invalid ] && [ "$brightness" -le 24 ] &&
   [ "$warmth" != invalid ] && [ "$warmth" -le 24 ]; then
    light_rc=0
    lipc-set-prop com.lab126.powerd flIntensity "$brightness" >/dev/null 2>&1 || light_rc=$?
    lipc-set-prop com.lab126.powerd currentAmberLevel "$warmth" >/dev/null 2>&1 || light_rc=$?
    echo "ANDROID_FRONTLIGHT_REAPPLY brightness=$brightness warmth=$warmth rc=$light_rc" >> "$LOG"
else
    echo "ANDROID_FRONTLIGHT_REAPPLY_SKIPPED brightness=$brightness warmth=$warmth" >> "$LOG"
fi

netns=$(readlink "/proc/$target/ns/net" 2>/dev/null || :)
case "$netns" in net:\[*\]) ;; *) echo NETWORK_RELAY_REFUSED=invalid_target_namespace >> "$LOG"; exit 7;; esac
echo "NETWORK_RELAY_START pid=$target comm=art-guard root=$stage/rootfs netns=$netns mtu=1500 dns=10.0.2.3" >> "$LOG"
resolver_file=$BASE/resolver-session-$launcher.conf
: > "$resolver_file"
chmod 600 "$resolver_file"
for server in $(awk '$1 == "nameserver" {print $2}' /etc/resolv.conf 2>/dev/null); do
    case "$server" in *[!0-9.]*|'') continue;; esac
    if /bin/busybox nslookup api.jdapi.com "$server" >/dev/null 2>&1; then
        printf 'nameserver %s\n' "$server" >> "$resolver_file"
        echo "NETWORK_RELAY_UPSTREAM_DNS=$server" >> "$LOG"
    else
        echo "NETWORK_RELAY_UPSTREAM_DNS_UNAVAILABLE=$server" >> "$LOG"
    fi
done
if [ -s "$resolver_file" ] && command -v unshare >/dev/null 2>&1; then
    unshare -m /bin/sh -c '
        resolver=$1; binary=$2; target=$3
        mount --bind "$resolver" /etc/resolv.conf || exit 97
        exec "$binary" --configure --mtu=1500 --disable-host-loopback --userns-path=/proc/"$target"/ns/user "$target" tap0
    ' sh "$resolver_file" "$BIN" "$target" >> "$LOG" 2>&1 &
    echo NETWORK_RELAY_PRIVATE_RESOLVER_MOUNT=1 >> "$LOG"
else
    echo NETWORK_RELAY_PRIVATE_RESOLVER_MOUNT=unavailable_using_host_configuration >> "$LOG"
    "$BIN" --configure --mtu=1500 --disable-host-loopback --userns-path=/proc/"$target"/ns/user "$target" tap0 >> "$LOG" 2>&1 &
fi
relay_pid=$!
stop_reason=

# Host hardware controls: one request only, strict keys and numeric ranges.
# No shell text supplied by Android is ever evaluated.
controls_tick=0
hardware_poll() {
    control_dir=/proc/$target/root/data/local/tmp
    [ -d "$control_dir" ] || return 0
    request=$control_dir/kindle-device-request
    if [ -f "$request" ] && [ ! -L "$request" ]; then
        IFS=' ' read -r key value extra < "$request" || :
        rm -f "$request"
        case "${value:-}" in ''|*[!0-9]*) value=invalid;; esac
        if [ -z "${extra:-}" ] && [ "$value" != invalid ]; then
            case "$key" in
                brightness) [ "$value" -le 24 ] && lipc-set-prop com.lab126.powerd flIntensity "$value" >/dev/null 2>&1 || :;;
                warmth) [ "$value" -le 24 ] && lipc-set-prop com.lab126.powerd currentAmberLevel "$value" >/dev/null 2>&1 || :;;
                wifi) [ "$value" -le 1 ] && lipc-set-prop com.lab126.wifid enable "$value" >/dev/null 2>&1 || :;;
            esac
        fi
        controls_tick=0
    fi
    if [ "$controls_tick" -eq 0 ]; then
        status=$control_dir/kindle-device-status
        if [ ! -L "$status" ] && [ ! -L "$status.new" ]; then
            { printf 'brightness=%s\n' "$(lipc-get-prop com.lab126.powerd flIntensity 2>/dev/null || echo 0)"
              printf 'warmth=%s\n' "$(lipc-get-prop com.lab126.powerd currentAmberLevel 2>/dev/null || echo 0)"
              printf 'wifi=%s\n' "$(lipc-get-prop com.lab126.wifid enable 2>/dev/null || echo 0)"
              printf 'connection=%s\n' "$(lipc-get-prop com.lab126.wifid cmState 2>/dev/null || echo UNKNOWN)"
              battery=unknown
              for capacity in /sys/class/power_supply/*/capacity; do
                  [ -r "$capacity" ] || continue
                  value=$(cat "$capacity" 2>/dev/null || :)
                  case "$value" in ''|*[!0-9]*) continue;; esac
                  if [ "$value" -ge 0 ] && [ "$value" -le 100 ]; then battery=$value; break; fi
              done
              printf 'battery=%s\n' "$battery"
              printf 'clock=%s\n' "$(TZ=CST-8 date +%H:%M)"
            } > "$status.new"
            chmod 644 "$status.new"; mv -f "$status.new" "$status"
        fi
    fi
    controls_tick=$(( (controls_tick + 1) % 3 ))
}

while kill -0 "$relay_pid" 2>/dev/null; do
    [ -r "$LOCK" ] || { stop_reason=session_lock_missing; break; }
    current=$(cat "$LOCK" 2>/dev/null || :)
    [ "$current" = "$launcher" ] || { stop_reason=launcher_changed; break; }
    kill -0 "$launcher" 2>/dev/null || { stop_reason=launcher_gone; break; }
    [ -d "/proc/$target" ] || { stop_reason=android_process_gone; break; }
    [ "$(readlink "/proc/$target/ns/net" 2>/dev/null || :)" = "$netns" ] || { stop_reason=namespace_changed; break; }
    hardware_poll
    sleep 1
done

if [ -n "$stop_reason" ]; then
    echo "NETWORK_RELAY_STOP=$stop_reason" >> "$LOG"
    if kill -0 "$relay_pid" 2>/dev/null; then
        kill "$relay_pid" 2>/dev/null || :
    fi
fi
if wait "$relay_pid" 2>/dev/null; then rc=0; else rc=$?; fi
relay_pid=
echo "NETWORK_RELAY_EXIT status=$rc" >> "$LOG"
exit 0
