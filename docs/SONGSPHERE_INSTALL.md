# SongSphere 1.0: install

[Back to the README](../README.md) · [What it does](DISCO_TOUR.md#songsphere)

SongSphere plays by mood. Place your albums and artists on a sphere (Calm to Energetic, Dark to Bright), then drag
to a mood and play. It is a separate app, installed once over Wi-Fi from your computer. SD updates of the interface
don't install, update or remove it, and your placements survive app updates.

It needs **diskOS Disco! 2.1.0 or later**: older versions open it, but can't play what it picks.

## What you need

- The Disc on the same network as your computer, with **Settings > System > Maintenance > Debug Mode** on. Debug
  Mode shows the address to use below; you can turn it off again afterwards.
- The complete release ZIP (`diskos-disco-<version>-complete.zip` from the
  [latest release](https://github.com/zmd22/diskos-disco/releases/latest)), extracted, or a copy of this repository.
  Both contain the `apps/songsphere` folder.
- A terminal: Linux or macOS, or Windows with WSL or PowerShell's `ssh`.

## Install or update

If SongSphere is open, close it first: Back to its first page, then **Exit**.

Open a terminal in the extracted folder and set the Disc's address once:

```sh
DISKOS_IP=192.168.1.50   # the address shown in Debug Mode
```

Then copy the app over. This checks the file before installing it, keeps the previous version as a backup, and
leaves your placements (`sphere.db`) alone:

```sh
tar -cf SongSphere-1.0.tar -C apps songsphere
ssh "root@$DISKOS_IP" '
set -eu
mkdir -p /usr/data/apps
stage=$(mktemp -d /usr/data/apps/.songsphere-install.XXXXXX)
trap "rm -rf \"$stage\"" EXIT
tar -xf - -C "$stage"
p=/usr/data/apps/songsphere
hash=$(sha256sum "$stage/songsphere/app")
test "${hash%% *}" = bfc3ee18b16ab5b4e698bfe3324eada47331116ebe2b99311b0e1cb698378f15
mkdir -p "$p"
if [ -f "$p/app" ]; then cp -p "$p/app" "$p/app.previous"; fi
cp "$stage/songsphere/app.conf" "$stage/songsphere/LICENSE" "$stage/songsphere/NOTICE.md" "$stage/songsphere/README.md" "$p/"
cp -R "$stage/songsphere/licenses" "$p/"
cp "$stage/songsphere/app" "$p/.app-new"
chmod 755 "$p/.app-new"
mv "$p/.app-new" "$p/app"
sync
' < SongSphere-1.0.tar
```

No output means it worked. If it stops with an error, nothing was replaced.

## Open it

All apps or Shortcuts, or add it to the navigation circle (Display > Disco Options > Disco Menu). Its first page
shows how many albums are placed.

- **Place music:** Albums or Artists, most played first. Tap one, drag the marker to where it sits, **Save**.
  A coloured dot means placed. Songs take their album's spot, else their artist's.
- **Play a mood:** drag the marker, tap **Play mood**, then **Play** or **Play and save** (kept as a playlist named
  after the mood and time). diskOS plays the songs in Shuffle and SongSphere closes.
- Only the marker moves (a tap elsewhere does nothing). Swipe right, or the round back button, goes back.

## Remove

```sh
ssh "root@$DISKOS_IP" 'rm -rf /usr/data/apps/songsphere'
```

This also deletes your placements. To keep them, copy `/usr/data/apps/songsphere/sphere.db` first.
