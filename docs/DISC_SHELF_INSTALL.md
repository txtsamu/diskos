# Disc Shelf 1.0.4: install and configure

[Back to the README](../README.md) · [What Disc Shelf does](DISCO_TOUR.md#disc-shelf)

Disc Shelf connects the Disc to your [Navidrome](https://www.navidrome.org/) server. Browse its artists and albums,
tick what you want, and it downloads the **original files** (no transcoding) to the SD card, where they play like
the rest of your library, offline.

It is a separate app, installed once over Wi-Fi from your computer. SD updates of the interface don't install,
update or remove it, and your settings survive app updates.

## What you need

- The Disc on the same network as your computer, with **Settings > System > Maintenance > Debug Mode** on. Debug
  Mode shows the address to use below; you can turn it off again afterwards.
- The complete release ZIP (`diskos-disco-<version>-complete.zip` from the
  [latest release](https://github.com/zmd22/diskos-disco/releases/latest)), extracted, or a copy of this repository.
  Both contain the `apps/disc-shelf` folder.
- Your Navidrome address, a username and its password. A separate Navidrome user for the Disc is a good idea (see
  [Privacy](#privacy)).
- A terminal: Linux or macOS, or Windows with WSL or PowerShell's `ssh`.

## 1. Install or update

If Disc Shelf is open, close it first: Back to its start page, then **Exit**.

Open a terminal in the extracted folder and set the Disc's address once:

```sh
DISKOS_IP=192.168.1.50   # the address shown in Debug Mode
```

Then copy the app over. This checks the file before installing it, keeps the previous version as a backup, and
leaves your `shelf.conf` alone:

```sh
tar -cf Disc-Shelf-1.0.4.tar -C apps disc-shelf
ssh "root@$DISKOS_IP" '
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
cp "$stage/disc-shelf/app.conf" "$p/app.conf"
cp "$stage/disc-shelf/shelf.conf.example" "$p/"
cp "$stage/disc-shelf/LICENSE" "$stage/disc-shelf/NOTICE.md" "$stage/disc-shelf/README.md" "$p/"
cp -R "$stage/disc-shelf/licenses" "$p/"
cp "$stage/disc-shelf/app" "$p/.app-new"
chmod 755 "$p/.app-new"
mv "$p/.app-new" "$p/app"
sync
' < Disc-Shelf-1.0.4.tar
```

No output means it worked. If it stops with an error, nothing was replaced. `app.conf` only holds the app's name
and the text of its landing page, so it is refreshed on every install.

## 2. Tell it about your server

Make a file called `shelf.conf` on your computer, starting from `apps/disc-shelf/shelf.conf.example`:

```ini
# Your Navidrome address, without /rest at the end
server=http://192.168.1.10:4533
username=YOUR_USERNAME
password=YOUR_PASSWORD
# Where downloads go: a folder on the SD card. Existing music is never overwritten.
download_dir=/tmp/sdcard/Navidrome
```

| Line | What to put there |
|---|---|
| `server` | The address you use for Navidrome in a browser, including `http://` or `https://` and the port. |
| `username`, `password` | A Navidrome login. |
| `download_dir` | A folder on the card. `/tmp/sdcard` is the card itself; the folder is created if needed. |

Send it to the Disc (this creates or replaces the file there, readable only by the system):

```sh
ssh "root@$DISKOS_IP" '
set -eu
umask 077
cat > /usr/data/apps/disc-shelf/.shelf.conf-new
chmod 600 /usr/data/apps/disc-shelf/.shelf.conf-new
mv /usr/data/apps/disc-shelf/.shelf.conf-new /usr/data/apps/disc-shelf/shelf.conf
sync
' < shelf.conf
```

Keep `shelf.conf` out of Git and out of anything you share: it holds your password. This repository already ignores
it. After changing it, close and reopen Disc Shelf.

## 3. Open it

- **From the navigation circle:** Display > Navigation > Menu items, add **Disc Shelf**. Choosing it shows its landing
  page; tap **Open Disc Shelf**.
- **From Shortcuts:** Display > Navigation > Shortcuts, pick Disc Shelf.
- **From All apps** in Shortcuts.

If it doesn't appear in the list yet, restart the screen once (Settings > System > Maintenance > Restart).

## 4. Use it

1. **Browse collection** opens your artists. Use the A-Z picker to jump (numbers and symbols included).
2. Open an artist to see all their albums in one list.
3. Tap the round selectors to choose artists or albums, then **Download**.
4. The copy page shows progress. You can cancel at any time; finished files stay.
5. Back steps up one level. **Exit** is on Disc Shelf's start page.
6. Rescan once you're done (Settings > System > Library > Rescan, or Quick Settings) so the new music appears.

A **lime outline** on a selector means some of that music is already on the card, not necessarily all of it.
Files that are already there are skipped, so downloading an album again only fetches what's missing.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `ssh: connect ... timed out` | Debug Mode on, same network, address typed correctly. |
| The install stops at `test` | The `app` file is damaged or from another version: extract the ZIP again. |
| Disc Shelf can't reach the server | Wi-Fi on; open `server` in a browser on the same network; no `/rest` at the end. |
| Login refused | Username and password in `shelf.conf`; try them on the Navidrome website. |
| HTTPS errors | Disc Shelf uses the player's own download tools and checks certificates. Use a valid certificate, or plain `http://` on your home network. Don't turn checking off. |
| Downloaded music doesn't show | Rescan. Check `download_dir` is on the card (`/tmp/sdcard/...`). |
| "Not enough space" or stalls | Free space on the SD card. |

## Go back to the previous version

Every install keeps the one before as `app.previous`. To restore it (close Disc Shelf first):

```sh
ssh "root@$DISKOS_IP" '
set -eu
p=/usr/data/apps/disc-shelf
cp "$p/app.previous" "$p/.app-new"
chmod 755 "$p/.app-new"
mv "$p/.app-new" "$p/app"
sync
'
```

## Remove it

```sh
ssh "root@$DISKOS_IP" 'rm -rf /usr/data/apps/disc-shelf && sync'
```

This removes the app and your `shelf.conf`. Music you downloaded stays on the card. Remove it from the Disco Menu
and Shortcuts too (an entry for a missing app simply disappears from the circle).

## Privacy

Your Navidrome password is stored as plain text on the Disc, in a file only the system can read. Anyone with
root access to the Disc (Debug Mode turns on SSH) could read it. Use a Navidrome user for the Disc with only the
access it needs, and turn Debug Mode off when you're done. Disc Shelf only talks to the server in `shelf.conf`.

## Licence and source

Disc Shelf is GPL-3.0-or-later. Its exact source is in the release as `disc-shelf-1.0.4-source.tar.gz`, and its
notices and licence texts are in `apps/disc-shelf/` (`NOTICE.md`, `licenses/`). The source archive is for building
on a computer; don't copy it to the Disc's apps folder.
