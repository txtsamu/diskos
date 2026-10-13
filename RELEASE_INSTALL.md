# Install diskOS Disco!

**On Windows:** download `diskOS-Disco-for-Windows.zip` from the release, extract it and run `diskOS-Disco.exe`. It
checks your PC, flashes the Disc and makes SD updates ([guide](docs/WINDOWS.md)).

**On Linux:** flash with the installer from the repository or source download, using your update key
([README](README.md#install)). **Already on 1.4.1 or later? 2.0 is an SD update.** Older than that? Reflash once first
(the 1.4.1 boot-script fixes can't arrive by SD card). See [RELEASE-NOTES.md](RELEASE-NOTES.md).

After that, `mq_ui` is the interface: sign it with your own keys and install it from the SD card
([docs/UPDATING.md](docs/UPDATING.md)).

Optional apps, installed separately:

- [Disc Shelf](docs/DISC_SHELF_INSTALL.md): Navidrome browser and original-file downloader.
- [Album Roulette](docs/ALBUM_ROULETTE_INSTALL.md): spin your collection for an album to play.

Boomshine is built into `mq_ui`: add it in Disco Options > Disco Menu. Installed apps can join that menu too.

This package is not a flasher. It contains no FiiO firmware, music, signing keys or account details.
