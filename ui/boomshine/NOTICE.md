# Attribution

This is an original native implementation inspired by Danny Miller / K2xL's
Boomshine and the chain-reaction games available in Rockbox. No original game,
Rockbox, or diskOS source code, music, or artwork is incorporated.
Original inspiration: https://www.k2xl.com/games/boomshine/

The screen and input mappings were checked against diskOS Disco's documented
app launcher and rotation behaviour. diskOS UI repository:
https://github.com/zmd22/diskos-disco

Embedded glyphs are generated from DejaVu Sans. The corresponding font files
and complete upstream copyright/license notice are included in the source
package. See fonts/LICENSE-DejaVu.txt.

The executable statically links musl libc and GCC runtime support. musl is MIT
licensed; GCC runtime components use the GCC Runtime Library Exception with
GPLv3. Corresponding notices accompany the package in licenses/.
