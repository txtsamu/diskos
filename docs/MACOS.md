# Install diskOS Disco! from macOS

Use a Mac, a USB data cable and your own unmodified **FiiO V2.57 firmware ZIP**. Back up your music and keep the installer's saved stock image on separate storage. Allow about 30 uninterrupted minutes.

This follows [b0hemia's diskOS 1.2.0 installation notes](https://github.com/b0hemia/diskos/tree/v1.2.0#install), adapted for Disco. Run commands as your normal user, **without sudo**.

## 1. Get Disco and Python

Download and unpack the [Disco 2.5.2 source archive](https://github.com/zmd22/diskos-disco/archive/refs/tags/v2.5.2.tar.gz). Open Terminal and enter its folder:

```sh
cd ~/Downloads/diskos-disco-2.5.2
python3 --version
uname -m
```

Python must be 3.8 or newer. Install it from [python.org](https://www.python.org/downloads/macos/) if needed; its installer also includes Tk for the graphical interface. Homebrew users can instead install `python3`; the GUI additionally needs the matching `python-tk@<major.minor>` package reported by `./install.sh`. The command-line installer does not need Tk.

`uname -m` should say `arm64` on Apple Silicon or `x86_64` on Intel. On Apple Silicon, use a native Terminal/Python session rather than Rosetta.

## 2. Get the Mac flash tools

### Apple Silicon: use the ready-built tools

Download these two files from [b0hemia/diskOS 1.2.0](https://github.com/b0hemia/diskos/releases/tag/v1.2.0) into `~/Downloads`:

- [diskos-installer-macos-arm64.tar.gz](https://github.com/b0hemia/diskos/releases/download/v1.2.0/diskos-installer-macos-arm64.tar.gz)
- [SHA256SUMS](https://github.com/b0hemia/diskos/releases/download/v1.2.0/SHA256SUMS)

Verify the download before extracting it:

```sh
(cd ~/Downloads && shasum -a 256 diskos-installer-macos-arm64.tar.gz)
```

The printed hash must match the line for that filename in `SHA256SUMS`. From the **Disco source folder**, copy only its native tools:

```sh
mkdir -p vendor
tar -xzf ~/Downloads/diskos-installer-macos-arm64.tar.gz \
  -C vendor --strip-components 2 diskos-installer/vendor/macos-arm64
```

This supplies `usbboot`, `mksquashfs`, `unsquashfs`, their libraries and device-loader files under `vendor/macos-arm64/`. The uploader source and device-loader files in upstream 1.2.0 match Disco's versions. Keep using **Disco's** installer and payload, not the upstream package's installer. No Homebrew tool build is needed on this route.

### Intel Mac: build native tools once

Install [Homebrew](https://brew.sh/) and Apple's command-line tools if they are missing:

```sh
xcode-select --install
```

From the Disco source folder:

```sh
bash vendor/setup-macos.sh
```

This builds `vendor/macos-x86_64/`. The script installs build dependencies through Homebrew and requires specific library versions; see [the pinned build inputs](../build/NATIVE_TOOLS.md#pinned-macos-arm64-build-inputs). If it reports a version mismatch, stop and follow its instructions rather than bypassing the check. Upstream 1.2.0 does not provide a ready-built Intel package.

## 3. Prepare Disco

```sh
./install.sh
./diskos-installer doctor
```

Setup creates a local Python environment. `doctor` reports missing components. A missing player is expected until it is connected in flashing mode.

For later signed SD-card updates, create your update keys once:

```sh
sh ui/tools/owner-keys/diskos-keys.sh
cp diskos-keys/diskos-root.pub.pem payload/
```

Keep the entire `diskos-keys` folder safe and private. Reuse your existing keys if you already made them. To install without your own update keys, skip this step and omit `--ota-key` below.

## 4. Flash the Disc

Keep the Mac awake. Power off the Disc, hold **Volume Down**, and connect USB. Its screen stays black in flashing mode. If macOS asks to allow the USB accessory, allow it.

From the Disco source folder, run the following, replacing the firmware path with your actual downloaded V2.57 ZIP:

```sh
./diskos-installer install \
  --firmware ~/Downloads/SNOWSKY_DISC_update_20260909_v257.zip \
  --variant public \
  --ui payload/mq_ui \
  --ota-key payload/diskos-root.pub.pem
```

Check the summary before confirming. With your own keys, compare its key fingerprint with:

```sh
shasum -a 256 payload/diskos-root.pub.pem
```

Wait for flashing and verification to finish before disconnecting or restarting. Public mode retains USB storage; temporary SSH is available through the Disc's Debug Mode.

The graphical installer is also available with `./diskos-installer gui`. Select Public, your official firmware ZIP, Disco's `payload/mq_ui` as the custom UI and your public update key. The command above makes those choices explicit.

## Updates and recovery

After installation, use [Disco's signed SD-card update instructions](UPDATING.md) with the same owner keys. You do not need to build `mq_ui` on your Mac.

If a verified downloaded tool is blocked, use macOS **System Settings → Privacy & Security** to review the blocked item. Keep the whole native-tool folder together, including `lib/`. A missing `usbboot` / E102 error usually means the tools are missing or the folder architecture does not match Python.

Keep the installer's saved stock image. To restore it, use `./diskos-installer restore-stock` and follow the prompts. Installation and USB flashing have not been exercised on a physical Mac during this documentation update.
