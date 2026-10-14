# Album Roulette

[Back to the README](../README.md) · [What it does](DISCO_TOUR.md#album-roulette)

**Since 2.2.0 Album Roulette is built into diskOS Disco!**: there is nothing to install. Update to 2.2.0 or later
and open it from the navigation circle, Apps, All apps or a shortcut. Your shortcuts and menu entries keep working.

## Coming from the separate app

The old app in `/usr/data/apps/album-roulette` is no longer used. Its cover cache on the card
(`.diskos/album-roulette`) is reused, so the first spin is quick. Remove the old app to free about 4 MB:

```sh
ssh "root@$DISKOS_IP" 'rm -rf /usr/data/apps/album-roulette'
```
