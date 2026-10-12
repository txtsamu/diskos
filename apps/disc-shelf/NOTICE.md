Disc Shelf 1.0.4 — GPL-3.0-or-later.

Uses diskOS / diskOS Disco! code by its contributors, from
https://github.com/zmd22/diskos-disco commit d9a3be3a140bfd8d790222fd9bec4f226db2feb7.
The source archive includes the used theme engine, controls, fonts and headers.
The standalone display and read-only theme adapters were reused from Album Roulette.
Library-style curved rows and navigation are adapted for a standalone downloader.
No stock database migrations, player-state writes or artwork-decoder processes are used.

LVGL 9.2.2: MIT; see licenses/LVGL.txt.
SQLite: public domain; see vendor/sqlite3.c.
Bundled font families: SIL Open Font License 1.1; see licenses/OFL-*.txt.
Additional DejaVu font license is retained with the source vendor set.
The source includes the GPL license and attribution for reused Disco components.
This is an independent homebrew app, not an official FiiO or Navidrome product.

Library A–Z picker: vendor/azjump.c and azjump.h are reused unchanged from the
Disco commit above; the app wrapper provides layout and persistent Exit.
