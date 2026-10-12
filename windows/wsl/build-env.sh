#!/bin/sh
# Build diskOS Disco!'s own WSL environment: a minimal Ubuntu 24.04 with the installer, its Python deps, a static
# usbboot and the current payload. Output: windows/out/diskos-wsl.tar.gz (imported by the Windows app).
# Run as root on Ubuntu/Debian x86_64 with debootstrap and network access to archive.ubuntu.com and PyPI.
set -eu
REPO=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
OUT="$REPO/windows/out"; R=${WORK:-/var/tmp/diskos-wslroot}
[ "$(id -u)" = 0 ] || { echo "run as root (debootstrap/chroot)" >&2; exit 1; }
mkdir -p "$OUT"; rm -rf "$R"

debootstrap --variant=minbase --components=main,universe \
  --include=python3,python3-venv,squashfs-tools,libusb-1.0-0,ca-certificates,xz-utils,unzip \
  noble "$R" http://archive.ubuntu.com/ubuntu

[ -x "$REPO/vendor/linux-x86_64/usbboot" ] || bash "$REPO/build/build-usbboot-static.sh"
mkdir -p "$R/opt/diskos"
git -C "$REPO" archive HEAD diskos-installer diskos_installer payload vendor requirements.txt LICENSE NOTICE.md licenses \
  | tar -x -C "$R/opt/diskos"
cp "$REPO/vendor/linux-x86_64/usbboot" "$R/opt/diskos/vendor/linux-x86_64/usbboot"

cp /etc/resolv.conf "$R/etc/resolv.conf"
CA=${PIP_CERT:-${REQUESTS_CA_BUNDLE:-}}
if [ -n "$CA" ] && [ -f "$CA" ]; then mkdir -p "$R$(dirname "$CA")"; cp "$CA" "$R$CA"; fi   # behind a TLS proxy
mount --bind /proc "$R/proc"
chroot "$R" /bin/sh -c 'cd /opt/diskos && python3 -m venv .venv && .venv/bin/pip install -q --no-cache-dir -r requirements.txt'
chroot "$R" /bin/sh -c 'HOME=/root /opt/diskos/diskos-installer doctor' || true
umount "$R/proc"
if [ -n "$CA" ] && [ -f "$R$CA" ]; then rm -f "$R$CA"; fi

cat > "$R/etc/wsl.conf" <<'E'
# diskOS Disco! for Windows: a private environment for building and flashing. Root, no systemd, no Windows PATH.
[user]
default=root
[boot]
systemd=false
[interop]
appendWindowsPath=false
[network]
hostname=diskos-disco
E
UI=$(sed -n 's/.*"version": "\(.*\)".*/\1/p' "$REPO/payload/mq_ui.build.json" | head -1)
echo "diskos-disco-wsl 1 $(date -u +%Y-%m-%d) ui-$UI" > "$R/opt/diskos/WSL_ENV_VERSION"
printf '#!/bin/sh\n# Run the diskOS installer from anywhere inside this environment.\nexec /opt/diskos/diskos-installer "$@"\n' > "$R/usr/local/bin/diskos"
chmod 755 "$R/usr/local/bin/diskos"

: > "$R/etc/resolv.conf"
rm -rf "$R"/var/cache/apt/archives/*.deb "$R"/var/lib/apt/lists/* "$R"/usr/share/doc/* "$R"/usr/share/man/* \
       "$R"/usr/share/info/* "$R"/usr/share/locale/* "$R"/var/log/*.log "$R/root/.cache"
find "$R/opt/diskos" -maxdepth 2 -name __pycache__ -prune -exec rm -rf {} +
SP="$R/opt/diskos/.venv/lib/python3.12/site-packages"           # not needed to run: pip, self-tests, Python's own tests/docs
rm -rf "$SP"/pip "$SP"/pip-* "$SP/Crypto/SelfTest" "$R"/opt/diskos/.venv/bin/pip* "$R"/var/cache/* \
       "$R"/usr/lib/python3.12/test "$R"/usr/lib/python3.12/pydoc_data "$R"/usr/lib/python3.12/idlelib \
       "$R"/usr/lib/python3.12/lib2to3 "$R"/usr/lib/python3.12/turtledemo "$R"/usr/share/python-wheels
tar --numeric-owner -C "$R" -cpf - . | gzip -9 > "$OUT/diskos-wsl.tar.gz"
# Also in parts of 25 MB for places with an upload limit; the app joins diskos-wsl.tar.gz.001, .002, ... itself.
rm -f "$OUT"/diskos-wsl.tar.gz.0*
split -b 25M -d -a 3 --numeric-suffixes=1 "$OUT/diskos-wsl.tar.gz" "$OUT/diskos-wsl.tar.gz."
(cd "$OUT" && sha256sum diskos-wsl.tar.gz > diskos-wsl.tar.gz.sha256)
rm -rf "$R"
ls -l "$OUT/diskos-wsl.tar.gz"; sha256sum "$OUT/diskos-wsl.tar.gz"
