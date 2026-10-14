# SongSphere

[Back to the README](../README.md) · [What it does](DISCO_TOUR.md#songsphere)

**Since 2.2.0 SongSphere is built into diskOS Disco!**: there is nothing to install. Update to 2.2.0 or later and
find it in the Disco Menu, Apps or All apps (Settings > Playback > SongSphere turns it on or off).

## Coming from the 2.1 add-on

Your placements carry over by themselves the first time SongSphere opens. The old separate app is no longer used;
remove it to free its space:

```sh
ssh "root@$DISKOS_IP" 'rm -rf /usr/data/apps/songsphere'
```

Do that after you have opened SongSphere once on 2.2.0 (it reads the old placements from that folder).
