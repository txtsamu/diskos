# Disc Shelf 1.0.4 - install and configure

Disc Shelf is a separate Navidrome browser and original-file downloader. The exact
1.0.4 app binary has already been tested by the owner. A signed mq_ui update does
not install or update apps. Use All apps to open it, or add it to Shortcuts.

## Install or update

Close the running app: Back to the landing screen, then Exit. Extract the complete
Disco ZIP on your computer and open a terminal in its directory. From a Git checkout,
the same `apps/disc-shelf` directory is present.

```sh
tar -cf Disc-Shelf-1.0.4.tar -C apps disc-shelf
ssh root@192.168.178.63 '
set -eu
mkdir -p /usr/data/apps
stage=$(mktemp -d /usr/data/apps/.shelf-install.XXXXXX)
trap "rm -rf \"$stage\"" EXIT
tar -xf - -C "$stage"
p=/usr/data/apps/disc-shelf
hash=$(sha256sum "$stage/disc-shelf/app")
test "${hash%% *}" = 87ce392f42431bb5119ffa3b0e0f8528ebbd1a6ba43a6b3cc88b3f77fbb6094d
mkdir -p "$p"
if [ -f "$p/app" ]; then cp -p "$p/app" "$p/app.previous"; fi
if [ ! -f "$p/app.conf" ]; then cp "$stage/disc-shelf/app.conf" "$p/app.conf"; fi
cp "$stage/disc-shelf/shelf.conf.example" "$p/"
cp "$stage/disc-shelf/LICENSE" "$stage/disc-shelf/NOTICE.md" "$stage/disc-shelf/README.md" "$p/"
cp -R "$stage/disc-shelf/licenses" "$p/"
cp "$stage/disc-shelf/app" "$p/.app-new"
chmod 755 "$p/.app-new"
mv "$p/.app-new" "$p/app"
sync
' < Disc-Shelf-1.0.4.tar
```

Your existing `shelf.conf` is preserved. Reopen All apps; if needed restart the UI
normally so its installed-app list refreshes. No scp or adb is required.

## First-time configuration

Copy `apps/disc-shelf/shelf.conf.example` to a local file named `shelf.conf`, and
edit the server URL, username, password and destination. Use your Navidrome server's
actual URL. Keep credentials out of Git; the file is ignored by this repository.
Upload configuration only when you intend to create or replace it:

```sh
ssh root@192.168.178.63 '
set -eu
umask 077
cat > /usr/data/apps/disc-shelf/.shelf.conf-new
chmod 600 /usr/data/apps/disc-shelf/.shelf.conf-new
mv /usr/data/apps/disc-shelf/.shelf.conf-new /usr/data/apps/disc-shelf/shelf.conf
sync
' < shelf.conf
```

Close and reopen Disc Shelf after changing configuration. Credentials are stored
as plaintext in a root-readable file. HTTPS support follows the stock download
tools; do not disable certificate verification to work around a server issue.

## Use

Browse collection opens artists. The library A-Z picker loads all matching
artists, including numbers/symbols; opening an artist shows all its albums in
one continuous list. Select artists/albums using the round selectors, then tap
Download. A lime selector outline means local content was found, not necessarily
that every track in the album is already present.

The app downloads original files, skips existing matching files, and supports
cancellation. The copy page shows progress and recovery feedback. Back steps up
one level; Exit appears only on the landing screen. It follows diskOS theme and
appearance when launched. Rescan the Disc's local library after downloads.

To restore the previous app after closing it:

```sh
ssh root@192.168.178.63 '
set -eu
p=/usr/data/apps/disc-shelf
cp "$p/app.previous" "$p/.app-new"
chmod 755 "$p/.app-new"
mv "$p/.app-new" "$p/app"
sync
'
```

Corresponding source and licenses are included. The source archive is for building
on a computer; do not install it in the device's apps directory.
