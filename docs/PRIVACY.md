# diskOS privacy and network disclosure

This describes network use by the installer and the diskOS UI (`mq_ui`). The UI source
is published under GPL-3.0-or-later in [`ui/`](../ui/).

## The installer (this repo)

- **`./install.sh` uses `pip`** to upgrade `pip` itself and download two packages - `pyusb` and
  `pycryptodome` - from **PyPI** into a local `.venv`. That `pip`/PyPI traffic is the only network
  activity in setup. Configure `pip` to use your own mirror or offline package files if you prefer.
- **The installer app itself makes no network requests.** Building the image, decrypting/extracting
  your firmware, saving the recovery image, and flashing over USB are all fully local. It does not
  phone home and sends no telemetry.

## The diskOS UI (`mq_ui`) on the device

diskOS is a local music player. Weather on Home is **on by default**; turning it off in Settings > Display > Weather on Home stops automatic fetches and hides the glance. The table below describes when each online feature sends a request. Each request reveals the device's public IP to the service it contacts.

| Feature | Endpoint | Protocol | What is sent |
|---|---|---|---|
| **Weather** | `wttr.in` | HTTPS | With Weather on Home on, diskOS fetches at UI startup and later refreshes or retries when Wi-Fi works. With it off, automatic fetches stop, but changing or resetting location in the Weather app explicitly requests weather. A request already in flight may still finish after you turn it off. It sends your configured location, or no location so wttr.in can estimate one from your public IP. |
| **Online album art** | `itunes.apple.com`, `*.mzstatic.com` | HTTPS | When Online Album Art is enabled and a playing track has no cover, artist and album or title metadata, plus a storefront country selected from the player's language, are sent to search for artwork. Matching images are downloaded from Apple's image hosts. Off by default. |
| **Lyrics** | `lrclib.net` | HTTPS | Online Lyrics is on by default. Opening Lyrics for a song without local or embedded words sends the current track's **title and artist** to find matching lyrics, if Online Lyrics is enabled. |
| **Update check and download** | `api.github.com`, `github.com` | HTTPS | Opening Update diskOS checks the latest release. The 1.2.1 release image includes the diskOS release key by default; confirming a supported update downloads its signed app bundle. Switching Allow diskOS Updates off prevents download and staging but still permits a release check. |

### Last.fm is no longer part of diskOS

Earlier versions could scrobble to Last.fm. That feature has been removed and diskOS no longer contacts
Last.fm. If you used it, your old API key, secret and session key may still sit in the device config
(`/usr/data`), and an unsent queue in `/usr/data/lastfm.queue`. Nothing reads or sends them any more; you
can delete those entries (the `lastfm_*` lines in the config) and that file if you want them gone.

### The stock FiiO player still runs alongside diskOS

diskOS replaces only the UI; FiiO's original player process still runs underneath it. That stock
component is responsible for Bluetooth, FiiO firmware-update checks, and LAN media
endpoints (DLNA/AirPlay/Roon/QPlay-style discovery) the device may present on your local network. Those
behaviours are inherited from the stock firmware and are outside diskOS's code. On V2.57, the stock player also performs an `ipinfo.io` time-zone lookup and network time synchronization after Wi-Fi connects when Automatic Time is enabled; changing that setting takes effect after the player next starts.

## What diskOS does NOT do

- No analytics, telemetry, or usage tracking.
- It does not upload your music library, your listening history, or any personal files.

## Reporting

Report anything that looks like unexpected data leaving the device via the process in
[`SECURITY.md`](../SECURITY.md).
