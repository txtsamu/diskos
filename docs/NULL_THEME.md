# Null — retained in diskOS Disco! 2.5.0

Select Display > Theme > Null. Null uses the same Display menu as every theme ([Display settings](DISPLAY_SETTINGS.md)): Appearance > Colour scheme picks its palette (including Matrix, since 2.5.3), Music screen holds fonts, background and information, Navigation holds Menu items and Quick settings. Null is always dark and right-handed, so those choices are dimmed. Since 2.5.4 its list and Music text is larger, and holding the progress line shows a time preview before seeking. Older notes below keep their original paths (such as Null Options).

Null stays the complete pixel theme alongside the new Squash. Its existing appearance, font/background choices, drawer and vertical QS remain. Menu items and Quick settings lead its options; Music and Settings cannot be removed from the main menu. Help > How to explains the gestures; hint settings are in Display > Navigation, saved independently per theme. [2.5.0 notes](releases/v2.5.0.md) · [Squash](themes/squash.md).

The Null.N entries below retain the original device-test history; current cumulative fixes are described in the 2.4.0 polish and shared Help sections.

Includes the pixel typography and transports, cyan/yellow and red/lime palettes,
sliding navigation, vertical 4-cover-4 quick settings, background choices,
text alignment, and buttons/autofade/gesture controls. Custom app interiors are retained.

Built with GCC 11.2.0 for static MIPS32r2, FP64, NaN2008.
Key host interaction checks and the transport checks passed. Physical Disc
validation remains pending. This is a development candidate, not a stable release.

Use the existing signed SD installation workflow and recovery/rollback procedure.
The raw mq_ui is the UI payload, not a signed SD update package.

Matching source is available from the release tag and source archives. Build with the
MIPS toolchain workflow documented in [ui/README.md](../ui/README.md).
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

Null.5 device-test candidate includes every source preview above. Charging QS uses
semantic green, an upward moving highlight and a flashing bolt; animation pauses
when QS closes. Below 15% the battery fill and icon are red; 15% remains normal.
Shared Boomshine/Stats/installed-app landings and Roulette have readable outlined
Null buttons. Transparent-button labels retain contrast elsewhere as well.
The screensaver keeps its custom fonts on entry: a 70px clock, separate 32px weather
symbol, 24px weather text/title, and 20px date/artist. 12-hour AM/PM is retained in
the date line so the large time fits. Unknown/absent weather icons are handled.
Focused battery, landing-button, weather-glyph, gesture, volume and routing checks
have passed on the host; physical Disc validation remains pending.

## Null.6 typography and screen refinements

- Music fonts: Original, Jersey (Jersey 10 + Pixelify Sans), Handjet (bold square
  Handjet titles + VT323 details). International fallback remains enabled.
- Null EQ: straight thin off-colour rails, on-colour fill and square heads.
  Existing wide touch regions and gain/selection handling remain unchanged.
- Stats retains Disco fonts, card shapes, headers and position dots; semantic
  colours use the current Null palette.
- Battery body text shifts 14 pixels right; title stays fixed. Full-charge age
  uses one compact line, including “ago”.

## Radio and Track Info reskin preview

Bluetooth and Wi-Fi use builder-owned flat headers, a connected device/network
summary, rectangular scan action, enlarged toggle target and the original
scrolling device/network rows. Track Info has a compact artwork/identity header
and scrollable terminal key/value sheet. Actions and data workers remain shared.

Fourth Null font option: Matrix. Dot Matrix headings and large Music title/clock,
VT323 details at 18/21/24/27 pixels; existing international fallback chains remain.
Original/Jersey/Handjet are retained. Choosing fonts reloads the themed UI.
Included in the cumulative 2.3.9 device-test build.

## QS layout preview

Null Options > QS layout: Text (default) or Icons. The icon option shares Disco’s glyph catalogue and live playback-source icon,
using Null’s idle/active palette in a two-column, four-row grid. Existing touch
targets, ordering, tap/hold callbacks and transport remain unchanged.

## Settings and guidance

Shared settings use separate selector and description areas. Editable detail
rows show a chevron; On/Off and read-only values remain visible. Overflow text
scrolls. Null options prioritize menu items, quick items, palette and background;
Autohide is now Close nav bar. Both themes have Settings > Help below Battery, with How to first,
with eight compact sections and contextual gesture hints with independent counters.

## 2.4.0 polish

Null is right-handed only; Side remains visible but disabled with an explanation.
How to uses full-topic snapped pages and a bottom next arrow. First-use tips
appear centrally, and the handle hint names the configured shortcuts. A-Z uses
50% black; the two QS side bars have their own 30% black strips. Shared settings
selectors are vertically centred with larger values and help. Library names
abbreviate until focused; Music names run three scroll cycles then rest.

## Shared Help menu

Disco and Null both place Help directly below Battery in Settings. Its order is
How to, First-use hints, Show hints again. Null no longer duplicates these in
Null Options. Each theme retains its own saved hint mode and learning counters.
Back from How to returns to Help; Back from Help returns to Settings.

QS editing shares Disco’s add/remove/reorder behaviour. All items are editable;
Null keeps up to eight, in saved order, with unused fields hidden. Changes are
applied on editor close and survive reopening.
