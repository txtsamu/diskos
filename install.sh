#!/bin/sh
set -eu

HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$HERE"

if [ "$(id -u)" = "0" ]; then
    echo "error: don't run install.sh as root (no sudo). Run it as your normal user;" >&2
    echo "       only the one-time udev rule needs root (see README, 'USB permissions')." >&2
    exit 1
fi

ERRTMP=$(mktemp "${TMPDIR:-/tmp}/diskos_venv.XXXXXX") || { echo "error: mktemp failed" >&2; exit 1; }
trap 'rm -f "$ERRTMP"' EXIT INT TERM

PY=""
for c in python3 python; do
    if command -v "$c" >/dev/null 2>&1; then
        if "$c" -c 'import sys; sys.exit(0 if sys.version_info[:2] >= (3, 8) else 1)' 2>/dev/null; then
            PY=$(command -v "$c"); break
        fi
    fi
done
if [ -z "$PY" ]; then
    echo "error: Python 3.8+ not found." >&2
    echo "  Arch/CachyOS:  sudo pacman -S python" >&2
    echo "  Debian/Ubuntu: sudo apt install python3 python3-venv" >&2
    echo "  Fedora:        sudo dnf install python3" >&2
    echo "  macOS:         brew install python3   (or install from python.org)" >&2
    exit 1
fi
echo "==> using $("$PY" --version 2>&1) at $PY"

if [ ! -x "$HERE/.venv/bin/python" ] && [ ! -x "$HERE/.venv/Scripts/python.exe" ]; then
    echo "==> creating virtual environment in .venv/"
    if ! "$PY" -m venv "$HERE/.venv" 2>"$ERRTMP"; then
        echo "error: could not create the virtual environment:" >&2
        sed 's/^/  /' "$ERRTMP" >&2 2>/dev/null || true
        echo "  Debian/Ubuntu often needs: sudo apt install python3-venv" >&2
        echo "  Arch/CachyOS: python includes venv in core package (reinstall: sudo pacman -S python)" >&2
        exit 1
    fi
else
    echo "==> reusing existing .venv/"
fi
VENV_PY="$HERE/.venv/bin/python"
[ -x "$VENV_PY" ] || VENV_PY="$HERE/.venv/Scripts/python.exe"

echo "==> installing dependencies (pyusb, pycryptodome)"
"$VENV_PY" -m pip install --upgrade pip >/dev/null 2>&1 || true
"$VENV_PY" -m pip install -r "$HERE/requirements.txt"

echo "==> checking optional system components"

if "$VENV_PY" -c 'import tkinter' 2>/dev/null; then
    echo "  ok: tkinter present - the graphical installer will work"
else
    echo "  note: tkinter NOT found - the GRAPHICAL installer will not run (the CLI still works)."
    echo "        To enable the GUI, install Tk for this Python, then re-run this script:"
    echo "          Arch/CachyOS:  sudo pacman -S tk"
    echo "          Debian/Ubuntu: sudo apt install python3-tk"
    echo "          Fedora:        sudo dnf install python3-tkinter"
    echo "          macOS:         brew install python-tk@$("$PY" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
    echo "                         (or use python.org's Python, which bundles Tk)"
fi

TAG=$("$VENV_PY" -c 'from diskos_installer import platform_probe; print(platform_probe.host_tag())' 2>/dev/null || true)
BUNDLED_LIBUSB=0
for f in "$HERE/vendor/$TAG/lib/"libusb-1.0*.dylib "$HERE/vendor/$TAG/lib/"libusb-1.0.so*; do
    [ -f "$f" ] && BUNDLED_LIBUSB=1
done
if [ "$BUNDLED_LIBUSB" -eq 1 ]; then
    echo "  ok: bundled libusb-1.0 present - device detection will use it"
elif "$VENV_PY" -c 'import ctypes.util,sys; sys.exit(0 if ctypes.util.find_library("usb-1.0") else 1)' 2>/dev/null; then
    echo "  ok: libusb-1.0 present - device detection will work"
else
    echo "  note: libusb-1.0 NOT found - the tool cannot detect the device until you install it:"
    echo "          Arch/CachyOS:  sudo pacman -S libusb"
    echo "          Debian/Ubuntu: sudo apt install libusb-1.0-0"
    echo "          Fedora:        sudo dnf install libusb1"
    echo "          macOS:         brew install libusb"
fi

if [ -n "$TAG" ] && [ -x "$HERE/vendor/$TAG/mksquashfs" ] && [ -x "$HERE/vendor/$TAG/unsquashfs" ]; then
    echo "  ok: bundled squashfs-tools present"
elif command -v mksquashfs >/dev/null 2>&1 && command -v unsquashfs >/dev/null 2>&1; then
    echo "  ok: system squashfs-tools present on PATH"
else
    echo "  note: squashfs-tools NOT found - needed to unpack and build firmware images:"
    echo "          Arch/CachyOS:  sudo pacman -S squashfs-tools"
    echo "          Debian/Ubuntu: sudo apt install squashfs-tools"
    echo "          Fedora:        sudo dnf install squashfs-tools"
    echo "          macOS:         brew install squashfs"
fi

if [ -n "$TAG" ] && [ -x "$HERE/vendor/$TAG/usbboot" ]; then
    echo "  ok: bundled usbboot present"
else
    echo "  note: usbboot not built yet (needed to flash in mask-ROM mode)."
    case "$TAG" in
        macos-*) echo "        Build it once with: bash vendor/setup-macos.sh" ;;
        linux-*) echo "        Build it once with: bash build/build-usbboot-static.sh" ;;
        *) echo "        See build/NATIVE_TOOLS.md for supported host builds." ;;
    esac
fi

echo
echo "Setup complete. Run the installer with:"
echo "    ./diskos-installer doctor      # check host + tools + device"
echo "    ./diskos-installer gui         # graphical installer (needs tkinter)"
echo "    ./diskos-installer --help      # all commands"
