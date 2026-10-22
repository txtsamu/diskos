/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PHONEHOME_H
#define PHONEHOME_H
/* Optional "Block FiiO update checks" (cfg block_fiio_checks, default 0 = off, stock behaviour).
 * On: the stock player's firmware-update and Bluetooth-firmware version lookups (wget to discpick.fiio.net and
 * fiio-bluetooth.fiio.net after Wi-Fi connects) are pointed at 127.0.0.1 through a private copy of /etc/hosts
 * bind-mounted over the read-only one. NTP, time zone, lyrics, weather, Last.fm and diskOS updates are untouched.
 * Off: the bind mount is removed and the stock /etc/hosts shows again. Idempotent; safe to call any time. */
void phonehome_apply(void);
#endif
