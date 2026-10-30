#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors

OPENSSL=/usr/bin/openssl
[ -x "$OPENSSL" ] || OPENSSL=openssl
MAXBYTES=8192

reject() { echo "REJECT: $1" >&2; exit 1; }

_sane_file() {
    _f="$1"; _max="$2"
    [ -e "$_f" ] || reject "missing file: $_f"
    [ -L "$_f" ] && reject "refuse symlink: $_f"
    [ -f "$_f" ] || reject "not a regular file: $_f"
    _sz=$(wc -c < "$_f" 2>/dev/null | tr -d ' ')
    case "$_sz" in ''|*[!0-9]*) reject "unsizable: $_f";; esac
    [ "$_sz" -le "$_max" ] || reject "oversize ($_sz > $_max): $_f"
    return 0
}
_sane_text() {
    _sane_file "$1" "$2"
    _bad=$(LC_ALL=C tr -d '\012\040-\176' < "$1" | wc -c | tr -d ' ')
    [ "$_bad" = 0 ] || reject "non-ASCII/control byte in $1"
    return 0
}

_field() {
    _m="$1"; _k="$2"
    _n=$(grep -c "^$_k=" "$_m" 2>/dev/null || echo 0)
    [ "$_n" = 1 ] || reject "field '$_k' count=$_n (want exactly 1)"
    _line=$(grep "^$_k=" "$_m")
    case "$_line" in *"$(printf '\r')"*) reject "CR in field '$_k'";; esac
    printf '%s' "${_line#"$_k="}"
}

_check_schema() {
    _sf="$1"; _keys=" $2 "; _seen=" "; _ln=0
    while IFS= read -r _l || [ -n "$_l" ]; do
        _ln=$((_ln + 1))
        [ "$_ln" -eq 1 ] && continue
        case "$_l" in *=?*) ;; *) reject "$_sf line $_ln: not KEY=VALUE with a value";; esac
        _kk="${_l%%=*}"
        case "$_keys" in *" $_kk "*) ;; *) reject "$_sf: unknown field '$_kk'";; esac
        case "$_seen" in *" $_kk "*) reject "$_sf: duplicate field '$_kk'";; esac
        _seen="$_seen$_kk "
    done < "$_sf"
    for _kk in $2; do
        case "$_seen" in *" $_kk "*) ;; *) reject "$_sf: missing field '$_kk'";; esac
    done
    return 0
}

_is_hex64() { case "$1" in *[!0-9a-f]*) return 1;; esac; [ "${#1}" = 64 ]; }
_is_uint()  { case "$1" in ''|*[!0-9]*) return 1;; esac; [ "${#1}" -le 18 ]; }
_sha256()   { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

_verify_sig() {
    if "$OPENSSL" dgst -sha256 -verify "$1" -signature "$2" "$3" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

ROOTPUB="$1"; MIN_KEY_EPOCH="$2"; MIN_UI_EPOCH="$3"; BDIR="$4"; UIBIN="$5"
[ -n "$ROOTPUB" ] && [ -n "$MIN_KEY_EPOCH" ] && [ -n "$MIN_UI_EPOCH" ] && [ -n "$BDIR" ] && [ -n "$UIBIN" ] \
    || reject "usage: diskos-verify.sh rootpub MIN_KEY_EPOCH MIN_UI_EPOCH bundle_dir ui_binary"
_is_uint "$MIN_KEY_EPOCH" || reject "bad MIN_KEY_EPOCH"
_is_uint "$MIN_UI_EPOCH"  || reject "bad MIN_UI_EPOCH"
_sane_file "$ROOTPUB" 4096

AUTHZ="$BDIR/keyauthz"; AUTHZ_SIG="$BDIR/keyauthz.sig"; LEAFPUB="$BDIR/leafpub.pem"
MAN="$BDIR/update.manifest"; MAN_SIG="$BDIR/update.manifest.sig"

_sane_text "$AUTHZ" "$MAXBYTES"; _sane_file "$AUTHZ_SIG" 4096; _sane_file "$LEAFPUB" 4096
[ "$(head -n1 "$AUTHZ")" = "diskos-keyauthz-v1" ] || reject "authz: bad magic"
_verify_sig "$ROOTPUB" "$AUTHZ_SIG" "$AUTHZ" || reject "authz: root signature invalid"
_check_schema "$AUTHZ" "keyid keyepoch role pubkey_sha256"
KEYEPOCH=$(_field "$AUTHZ" keyepoch) || exit 1; _is_uint "$KEYEPOCH" || reject "authz: bad keyepoch"
[ "$KEYEPOCH" -ge "$MIN_KEY_EPOCH" ] || reject "authz: keyepoch $KEYEPOCH < floor $MIN_KEY_EPOCH (revoked key)"
ROLE=$(_field "$AUTHZ" role) || exit 1; [ "$ROLE" = "tier-a" ] || reject "authz: role '$ROLE' != tier-a"
PUBHASH=$(_field "$AUTHZ" pubkey_sha256) || exit 1; _is_hex64 "$PUBHASH" || reject "authz: bad pubkey_sha256"
[ "$(_sha256 "$LEAFPUB")" = "$PUBHASH" ] || reject "authz: leafpub hash mismatch (substituted key)"

_sane_text "$MAN" "$MAXBYTES"; _sane_file "$MAN_SIG" 4096
[ "$(head -n1 "$MAN")" = "diskos-update-v1" ] || reject "manifest: bad magic"
_verify_sig "$LEAFPUB" "$MAN_SIG" "$MAN" || reject "manifest: leaf signature invalid"
_check_schema "$MAN" "component epoch version size sha256 channel"
COMPONENT=$(_field "$MAN" component) || exit 1; [ "$COMPONENT" = "mq_ui" ] || reject "manifest: component '$COMPONENT' != mq_ui"
UEPOCH=$(_field "$MAN" epoch) || exit 1; _is_uint "$UEPOCH" || reject "manifest: bad epoch"
[ "$UEPOCH" -ge "$MIN_UI_EPOCH" ] || reject "manifest: epoch $UEPOCH < baked floor $MIN_UI_EPOCH (downgrade)"
MSIZE=$(_field "$MAN" size) || exit 1;   _is_uint "$MSIZE"  || reject "manifest: bad size"
MSHA=$(_field "$MAN" sha256) || exit 1;  _is_hex64 "$MSHA"  || reject "manifest: bad sha256"
VERSION=$(_field "$MAN" version) || exit 1

_sane_file "$UIBIN" 33554432
_usz=$(wc -c < "$UIBIN" | tr -d ' ')
[ "$_usz" = "$MSIZE" ] || reject "binary size $_usz != manifest $MSIZE"
[ "$(_sha256 "$UIBIN")" = "$MSHA" ] || reject "binary sha256 mismatch"
[ "$(od -An -tx1 -N4 "$UIBIN" | tr -d ' ')" = "7f454c46" ] || reject "binary: not ELF"
[ "$(od -An -tx1 -j4 -N2 "$UIBIN" | tr -d ' ')" = "0101" ]  || reject "binary: not ELF32-LE"
[ "$(od -An -tx1 -j18 -N2 "$UIBIN" | tr -d ' ')" = "0800" ] || reject "binary: not MIPS"

echo "OK component=$COMPONENT version=$VERSION epoch=$UEPOCH keyepoch=$KEYEPOCH"
exit 0
