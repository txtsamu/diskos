#!/bin/sh

SELF="${DISKOS_SELF:-/usr/data/sshd}"
DB="$SELF/dropbearmulti"
KEYS="$SELF/keys"
SHADOW="$SELF/shadow"
SERIALPID="$SELF/serial.pid"
BUNDLED=/usr/project/dropbearmulti
CFGFS="${DISKOS_CONFIGFS:-/sys/kernel/config}"
UDCDIR="${DISKOS_UDC_DIR:-/sys/class/udc}"
ROLEF="${DISKOS_USB_ROLE:-/sys/class/usb_role/13500000.otg_new-role-switch/role}"
SHELL_TTY="${DISKOS_TTY_SHELL:-/dev/ttyGS0}"
RUN="${DISKOS_USBDBG_RUN:-/tmp/.diskos_usbdbg}"
UG="$CFGFS/usb_gadget/diskos_dbg"

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
    tr '\0' ' ' < "/proc/$_sp/cmdline" 2>/dev/null | grep -q "$SHELL_TTY"
}

serial_on() {
    [ -c "$SHELL_TTY" ] || { echo "no ttyGS0"; return 1; }
    if serial_alive; then echo on; return 0; fi
    rm -f "$SERIALPID" 2>/dev/null
    setsid sh -c 'echo $$ > '"$SERIALPID"'; while true; do /bin/sh <'"$SHELL_TTY"' >'"$SHELL_TTY"' 2>&1; sleep 1; done' </dev/null >/dev/null 2>&1 &
    serial_alive || sleep 1
    serial_alive || sleep 1
    if serial_alive; then echo on; return 0; fi
    echo "err: serial supervisor did not start"; return 1
}

serial_stop() {
    if serial_alive; then kill "$(cat "$SERIALPID" 2>/dev/null)" 2>/dev/null; fi
    rm -f "$SERIALPID" 2>/dev/null
    fuser -k "$SHELL_TTY" 2>/dev/null
}

serial_off() {
    if usbdbg_bound; then echo "on (owned by USB Debug)"; return 0; fi
    serial_stop
    echo off
}

status() {
    s=off; pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && s=on
    r=off; serial_alive && r=on
    k=off; key_up && k=on
    echo "SSH=$s SERIAL=$r KEYSSH=$k"
}

usbdbg_bound() { [ -n "$(tr -d ' \n' 2>/dev/null < "$UG/UDC")" ]; }

usbdbg_state() {
    if [ ! -d "$UG" ]; then echo off; elif usbdbg_bound; then echo on; else echo unbound; fi
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

stock_gadget_present() {
    for _g in uac_demo storage_demo adb_demo; do
        [ -d "$CFGFS/usb_gadget/$_g" ] && return 0
    done
    return 1
}

udc_name() { ls "$UDCDIR" 2>/dev/null | head -n 1; }

wait_udc() {
    _i=0
    while [ "$_i" -lt 10 ]; do
        _u=$(udc_name)
        [ -n "$_u" ] && { echo "$_u"; return 0; }
        sleep 1; _i=$((_i+1))
    done
    return 1
}

role_take() {
    [ -w "$ROLEF" ] || return 0
    mkdir -p "$RUN" 2>/dev/null
    _cur=$(cat "$ROLEF" 2>/dev/null)
    [ -f "$RUN/role.prev" ] || echo "$_cur" > "$RUN/role.prev" 2>/dev/null
    [ "$_cur" = device ] && return 0
    echo device > "$ROLEF" 2>/dev/null
    sleep 1
    return 0
}

role_give_back() {
    [ -f "$RUN/role.prev" ] || return 0
    _prev=$(cat "$RUN/role.prev" 2>/dev/null)
    rm -f "$RUN/role.prev"
    [ -w "$ROLEF" ] || return 0
    [ "$(cat "$ROLEF" 2>/dev/null)" = device ] && [ -n "$_prev" ] && [ "$_prev" != device ] && echo "$_prev" > "$ROLEF" 2>/dev/null
    return 0
}

cfgfs_up() {
    [ -d "$CFGFS/usb_gadget" ] && return 0
    mkdir -p "$RUN" 2>/dev/null
    mount -t configfs none "$CFGFS" 2>/dev/null || return 1
    : > "$RUN/mounted"
    [ -d "$CFGFS/usb_gadget" ]
}

usbdbg_build() {
    mkdir "$UG" || return 1
    echo 0x0525 > "$UG/idVendor"
    echo 0xa4a7 > "$UG/idProduct"
    echo 0x0200 > "$UG/bcdUSB"
    echo 0x0100 > "$UG/bcdDevice"
    echo 0x02   > "$UG/bDeviceClass"
    echo 0x00   > "$UG/bDeviceSubClass"
    echo 0x00   > "$UG/bDeviceProtocol"
    mkdir -p "$UG/strings/0x409" "$UG/configs/c.1/strings/0x409" "$UG/functions/acm.0" || return 1
    echo "diskOS"            > "$UG/strings/0x409/manufacturer"
    echo "diskOS USB Debug"  > "$UG/strings/0x409/product"
    echo "diskos-debug"      > "$UG/strings/0x409/serialnumber"
    echo "serial shell"      > "$UG/configs/c.1/strings/0x409/configuration"
    echo 120  > "$UG/configs/c.1/MaxPower"
    echo 0x80 > "$UG/configs/c.1/bmAttributes"
    ln -s "$UG/functions/acm.0" "$UG/configs/c.1/acm.0" || return 1
}

usbdbg_unbind() {
    [ -d "$UG" ] || return 0
    echo "" > "$UG/UDC" 2>/dev/null
    _i=0
    while usbdbg_bound && [ "$_i" -lt 10 ]; do sleep 1; _i=$((_i+1)); done
    usbdbg_bound && return 1
    return 0
}

usbdbg_remove() {
    [ -d "$UG" ] || return 0
    rm -f "$UG/configs/c.1/acm.0"
    rmdir "$UG/functions/acm.0" 2>/dev/null
    rmdir "$UG/configs/c.1/strings/0x409" "$UG/configs/c.1" "$UG/strings/0x409" 2>/dev/null
    rmdir "$UG" 2>/dev/null
    [ -d "$UG" ] && return 1
    return 0
}

legacy_serial_remove() {
    _lg="$CFGFS/usb_gadget/serial_demo"
    [ -d "$_lg" ] || return 0
    echo "" > "$_lg/UDC" 2>/dev/null
    rm -f "$_lg/configs/c.1/acm.0"
    rmdir "$_lg/functions/acm.0" "$_lg/configs/c.1/strings/0x409" "$_lg/configs/c.1" "$_lg/strings/0x409" "$_lg" 2>/dev/null
    [ -d "$_lg" ] && return 1
    return 0
}

usbdbg_umount() {
    [ -f "$RUN/mounted" ] || return 0
    [ -z "$(ls "$CFGFS/usb_gadget" 2>/dev/null)" ] || return 0
    umount "$CFGFS" 2>/dev/null && rm -f "$RUN/mounted"
    return 0
}

wait_tty() {
    _i=0
    while [ "$_i" -lt 8 ]; do
        [ -c "$1" ] && return 0
        sleep 1; _i=$((_i+1))
    done
    return 1
}

usb_clean() {
    serial_stop
    usbdbg_unbind
    usbdbg_remove
    role_give_back
}

usb_abort() {
    usb_clean
    usbdbg_umount
}

usb_on() {
    trap '' HUP
    stock_gadget_present && { echo "err: another USB gadget is active"; return 2; }
    cfgfs_up || { echo "err: configfs unavailable"; return 5; }
    if usbdbg_bound && [ -c "$SHELL_TTY" ]; then
        serial_on >/dev/null 2>&1
        echo "on shell=$(serial_alive && echo on || echo off)"
        return 0
    fi
    legacy_serial_remove || { echo "err: could not remove the old serial gadget"; return 5; }
    usb_clean
    usbdbg_build || { usb_abort; echo "err: gadget build failed"; return 5; }
    _udc=$(wait_udc) || { usb_abort; echo "err: no USB device controller"; return 3; }
    role_take
    echo "$_udc" > "$UG/UDC" 2>/dev/null
    usbdbg_bound || { usb_abort; echo "err: could not bind $_udc"; return 4; }
    wait_tty "$SHELL_TTY" || { usb_abort; echo "err: serial port did not appear"; return 6; }
    serial_on >/dev/null 2>&1
    echo "on shell=$(serial_alive && echo on || echo off)"
    return 0
}

usb_off() {
    trap '' HUP
    serial_stop
    if ! usbdbg_unbind; then echo "err: could not unbind the gadget"; return 1; fi
    if ! usbdbg_remove; then echo "err: could not remove the gadget"; return 1; fi
    role_give_back
    usbdbg_umount
    echo off
    return 0
}

usb_status() {
    _udc=$(udc_name)
    _st=$(tr ' ' _ < "$UDCDIR/$_udc/state" 2>/dev/null)
    _sh=off; serial_alive && _sh=on
    echo "USBDBG=$(usbdbg_state) UDC=${_udc:-none} HOST=${_st:-none} SHELL=$_sh"
}

case "$1" in
    ssh-on)     ssh_on "$2" ;;
    ssh-off)    ssh_off ;;
    serial-on)  serial_on ;;
    serial-off) serial_off ;;
    status)     status ;;
    key-on)     key_on ;;
    key-off)    key_off ;;
    usb-on)     usb_on ;;
    usb-off)    usb_off ;;
    usb-status) usb_status ;;
    *) echo "usage: $0 {ssh-on <hash>|ssh-off|serial-on|serial-off|key-on|key-off|usb-on|usb-off|usb-status|status}"; exit 1 ;;
esac
