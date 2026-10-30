#!/bin/sh

SELF=/usr/data/sshd
DB="$SELF/dropbearmulti"
KEYS="$SELF/keys"
SHADOW="$SELF/shadow"
SERIALPID="$SELF/serial.pid"
BUNDLED=/usr/project/dropbearmulti

overlay_present() {
    grep -q ' /etc/shadow ' /proc/mounts 2>/dev/null
    _g=$?
    [ "$_g" -eq 1 ] && return 1
    return 0
}

ensure_db() {
    mkdir -p "$KEYS" 2>/dev/null
    if [ -f "$BUNDLED" ] && ! cmp -s "$BUNDLED" "$DB" 2>/dev/null; then
        rm -f "$DB.new" 2>/dev/null
        if cp "$BUNDLED" "$DB.new" 2>/dev/null && chmod 755 "$DB.new" 2>/dev/null && cmp -s "$BUNDLED" "$DB.new"; then
            mv -f "$DB.new" "$DB" 2>/dev/null
        fi
        rm -f "$DB.new" 2>/dev/null
    fi
    [ -f "$DB" ] && [ ! -x "$DB" ] && chmod 755 "$DB" 2>/dev/null
    [ -x "$DB" ] || return 1
    [ -s "$KEYS/ed25519_host_key" ] || "$DB" dropbearkey -t ed25519       -f "$KEYS/ed25519_host_key" >/dev/null 2>&1
    [ -s "$KEYS/rsa_host_key" ]     || "$DB" dropbearkey -t rsa -s 2048   -f "$KEYS/rsa_host_key"     >/dev/null 2>&1
    return 0
}

ssh_on() {
    hash="$1"
    [ -n "$hash" ] || { echo "err: no hash"; return 1; }
    ensure_db      || { echo "err: no dropbear binary"; return 1; }
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill "$p" 2>/dev/null; done
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 5 ]; do sleep 1; i=$((i+1)); done
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && { echo "err: could not stop existing dropbear"; return 1; }
    if overlay_present; then
        umount /etc/shadow 2>/dev/null
        overlay_present && { echo "err: shadow overlay stuck"; return 1; }
    fi
    [ -f /etc/shadow ] || { echo "err: no /etc/shadow"; return 1; }
    awk -v h="$hash" -F: 'BEGIN{OFS=":"} $1=="root"{$2=h} {print}' /etc/shadow > "$SHADOW" 2>/dev/null \
        || { echo "err: shadow gen failed"; return 1; }
    chmod 600 "$SHADOW" 2>/dev/null
    awk -F: -v h="$hash" '$1=="root" && $2==h{ok=1} END{exit ok?0:1}' "$SHADOW" 2>/dev/null \
        || { echo "err: shadow bad"; rm -f "$SHADOW"; return 1; }
    mount -o bind "$SHADOW" /etc/shadow 2>/dev/null || mount --bind "$SHADOW" /etc/shadow 2>/dev/null
    grep -q ' /etc/shadow ' /proc/mounts 2>/dev/null || { echo "err: overlay failed, refusing SSH"; return 1; }
    pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 || \
        "$DB" dropbear -p 22 -r "$KEYS/ed25519_host_key" -r "$KEYS/rsa_host_key" >/dev/null 2>&1
    if ! pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1; then
        umount /etc/shadow 2>/dev/null
        overlay_present && { sleep 1; umount /etc/shadow 2>/dev/null; }
        if overlay_present; then
            echo "err: dropbear did not start AND overlay stuck"; return 2
        fi
        echo "err: dropbear did not start"; return 1
    fi
    echo on
}

ssh_off() {
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill "$p" 2>/dev/null; done
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 4 ]; do sleep 1; i=$((i+1)); done
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 2 ]; do sleep 1; i=$((i+1)); done
    if pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1; then
        echo "err: dropbear still running - overlay kept, refusing to expose stock shadow"; return 1
    fi
    overlay_present && umount /etc/shadow 2>/dev/null
    if overlay_present; then
        echo "err: overlay stuck"; return 1
    fi
    echo off
}

serial_alive() {
    [ -f "$SERIALPID" ] || return 1
    _sp=$(cat "$SERIALPID" 2>/dev/null); [ -n "$_sp" ] || return 1
    kill -0 "$_sp" 2>/dev/null || return 1
    tr '\0' ' ' < "/proc/$_sp/cmdline" 2>/dev/null | grep -q 'ttyGS0'
}

serial_on() {
    [ -c /dev/ttyGS0 ] || { echo "no ttyGS0"; return 1; }
    if serial_alive; then echo on; return 0; fi
    rm -f "$SERIALPID" 2>/dev/null
    setsid sh -c 'echo $$ > '"$SERIALPID"'; while true; do /bin/sh </dev/ttyGS0 >/dev/ttyGS0 2>&1; sleep 1; done' </dev/null >/dev/null 2>&1 &
    serial_alive || sleep 1
    serial_alive || sleep 1
    if serial_alive; then echo on; return 0; fi
    echo "err: serial supervisor did not start"; return 1
}

serial_off() {
    if serial_alive; then kill "$(cat "$SERIALPID" 2>/dev/null)" 2>/dev/null; fi
    rm -f "$SERIALPID" 2>/dev/null
    fuser -k /dev/ttyGS0 2>/dev/null
    echo off
}

status() {
    s=off; pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && s=on
    r=off; serial_alive && r=on
    k=off; key_up && k=on
    echo "SSH=$s SERIAL=$r KEYSSH=$k"
}

KEYHOME="$SELF/root_home"
KEYPAT="$SELF/dropbear -s"

key_up() {
    _any=0
    for _t in /proc/net/tcp /proc/net/tcp6; do
        [ -r "$_t" ] || continue
        _any=1
        awk '$2 ~ /:08AE$/ && $4 == "0A" {f=1} END{exit f?0:1}' "$_t" 2>/dev/null && return 0
    done
    [ "$_any" -eq 1 ] && return 1
    pgrep -f "$KEYPAT" >/dev/null 2>&1
}

key_on() {
    [ -s "$KEYHOME/.ssh/authorized_keys" ] || { echo "no authorized_keys"; return 1; }
    ensure_db || { echo "err: no dropbear binary"; return 1; }
    chmod 700 "$KEYHOME" "$KEYHOME/.ssh" 2>/dev/null; chmod 600 "$KEYHOME/.ssh/authorized_keys" 2>/dev/null
    grep -q ' /root ' /proc/mounts 2>/dev/null || mount -o bind "$KEYHOME" /root 2>/dev/null \
        || { echo "err: cannot bind $KEYHOME over /root"; return 1; }
    ln -sf dropbearmulti "$SELF/dropbear" 2>/dev/null
    pgrep -f "$KEYPAT" >/dev/null 2>&1 || \
        "$SELF/dropbear" -s -p 2222 -r "$KEYS/ed25519_host_key" -r "$KEYS/rsa_host_key" >/dev/null 2>&1
    key_up || { sleep 1; key_up; } || { echo "err: key dropbear did not start"; return 1; }
    echo on
}

key_off() {
    for p in $(pgrep -f "$KEYPAT" 2>/dev/null); do kill "$p" 2>/dev/null; done
    i=0; while pgrep -f "$KEYPAT" >/dev/null 2>&1 && [ "$i" -lt 2 ]; do sleep 1; i=$((i+1)); done
    for p in $(pgrep -f "$KEYPAT" 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    sleep 1
    if pgrep -f "$KEYPAT" >/dev/null 2>&1; then echo "err: key dropbear still running"; return 1; fi
    if grep -q ' /root ' /proc/mounts 2>/dev/null; then
        umount /root 2>/dev/null || umount -l /root 2>/dev/null
        grep -q ' /root ' /proc/mounts 2>/dev/null && { echo "err: /root still bound"; return 1; }
    fi
    key_up && { echo "err: still listening on 2222"; return 1; }
    echo off
}

case "$1" in
    ssh-on)     ssh_on "$2" ;;
    ssh-off)    ssh_off ;;
    serial-on)  serial_on ;;
    serial-off) serial_off ;;
    status)     status ;;
    key-on)     key_on ;;
    key-off)    key_off ;;
    *) echo "usage: $0 {ssh-on <hash>|ssh-off|serial-on|serial-off|key-on|key-off|status}"; exit 1 ;;
esac
