# Null 2.3.0-Null.4 — Disc device-test build

Select Display > Theme > Null. Theme settings are under Display > Null Options.

Includes the pixel typography and transports, cyan/yellow and red/lime palettes,
sliding navigation, vertical 4-cover-4 quick settings, background choices,
text alignment, and buttons/autofade/gesture controls. Custom app interiors are retained.

Built with GCC 11.2.0 for static MIPS32r2, FP64, NaN2008.
Key host interaction checks and the transport checks passed. Physical Disc
validation remains pending. This is a development candidate, not a stable release.

Use the existing signed SD installation workflow and recovery/rollback procedure.
The raw mq_ui is the UI payload, not a signed SD update package.

Source is supplied in Null-source.tar.gz. Build with ui/tools/build_device.py
and the toolchain-gcc11.2 release toolchain documented in ui/README.md.
The build manifest records the source digest and exact binary checksums.

Null.2 changes: Music clock/status row with a persistent top-strip hold target,
Show Clock and independent Scroll options under Null Options; Disco's six-action
track overlay and mood pill; larger album text and a compact closed information
block that separates upward/downward at the artist/album boundary when navigation
opens. QS has a faint palette-coloured edge glow. Text alignment now affects only
Music's title, artist, album and time. Theme changes reject overlapping picks during
restart and failed saves; Null's styling callbacks check the running theme.

The changed Music, open drawer, popup and QS screens were rendered on the host.
Null.3 fixes the actual cause of the apparent Ring/Null mixing: ordinary
SCR_NOWPLAYING requests now route to Null Music, while explicit Immersive entries
use the shared record overlay and return to Music on close. Title hold, QS,
Shortcuts and the track-menu full-art action use the same guarded entry path.
A focused host regression passed for playback destinations and Immersive exit
with animations both enabled and disabled. Physical Disc confirmation is pending.

Null.4: The track popup uses pixel captions FAV / ALB / ART / QUE / LYR / INF.
A-Z sits at the bottom, library position feedback sits below the header, and
shared lists use a continuous palette-coloured scroll line with a pixel marker.
The volume overlay is a draggable horizontal strip, without a full-screen scrim;
it enters in 120 ms, dismisses on tap or timeout, and commits the final drag value.
QS moves its upper group and square cover down 12 px and its lower group down;
its close handle remains below the action hit areas. Brightness is 160 px tall,
paired with a battery indicator at the left: 100% at top, red at 15% or less,
green while charging, otherwise the palette's active colour.
Focused host renders and the volume adjustment/dismiss/reopen/timeout check passed.

Next source preview (not yet a device payload): Null Options > Fonts switches the
Music screen between Original, Dot matrix and Terminal. Slightly larger cached
faces keep Unicode fallback. The mode control precedes the fixed progress line;
the time line is elapsed/total/file-extension. QS reserves a date zone and uses
40px action hit areas on an even 44px pitch, with a 72px square cover between
the two groups. The selected font option applies live to Music only.

Focused source preview: QS uses the live clock instead of a date and labels its
battery and brightness indicators. Configuration screens and EQ hide the Null
navigation overlay, leaving chooser controls unobstructed. The EQ landing page
preserves its own Null typography and visible toggle state.
Gesture controls now use the metadata region, including the separate album row:
double-tap toggles playback and horizontal swipes change track. A delayed single
tap retains the track popup; holds retain Immersive. Hints are removed and a short
pixel transport icon confirms actions. Pointer checks passed for these routes.
Volume uses a 24px eased slide and fade, opening in 96ms and closing in 70ms;
repeated value updates preserve the running animation. The volume interaction
check passed on the host. These source previews have not replaced the last
compiled Null.4 payload.
