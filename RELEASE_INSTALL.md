# Install diskOS Disco!

> **Recommended: start from FiiO's stock V2.57 firmware.** Installing over upstream diskOS or another diskOS build is not supported and can be buggy; restore stock first.

**On Windows:** download `diskOS-Disco-for-Windows.zip` from the release, extract it and run `diskOS-Disco.exe`. It
checks your PC, flashes the Disc and makes SD updates ([guide](docs/WINDOWS.md)).

**On macOS:** follow the [Mac guide](docs/MACOS.md), including links to upstream’s ready-built Apple Silicon flash tools.

**On Linux:** flash with the installer from the repository or source download, using your update key
([README](README.md#install)). **Already on 1.4.1 or later? 2.5.4 is an SD update.** Older than that? Reflash once first
(the 1.4.1 boot-script fixes can't arrive by SD card). See [release notes](docs/releases/v2.5.4.md).

After that, `mq_ui` is the interface: sign it with your own keys and install it from the SD card
([docs/UPDATING.md](docs/UPDATING.md)).

Optional apps, installed separately:

- [Disc Shelf](docs/DISC_SHELF_INSTALL.md): Navidrome browser and original-file downloader.

Boomshine and [Album Roulette](docs/ALBUM_ROULETTE_INSTALL.md) are built into `mq_ui`: add them in the theme’s Menu items editor. Installed apps can join that menu too.

This package is not a flasher. It contains no FiiO firmware, music, signing keys or account details.

## Try Squash

Choose Display → Theme → Squash for big bold Music text, touch playback and the D! launcher. Keep Null or your existing theme if you prefer; their saved settings remain. Settings → Help, below Battery, contains How to and first-use hints. See [the 2.5.0 notes](docs/releases/v2.5.0.md).
