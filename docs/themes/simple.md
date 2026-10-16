# Simple — theme preview 0.5

A separate, opt-in theme on the 2.4.0 baseline. Existing presets keep their IDs,
configuration keys and presentation paths. Simple uses Disco's menus, track
popup, editor and system actions rather than duplicating their behaviour.

## Presentation

- Dark charcoal / off-white; orange emphasis and outlined round controls.
- Nunito Bold 700 at 22/30 px for general important text and 38/32/28 px for Music; Inter Medium for smaller
  detail, with existing international and symbol fallback chains.
- Music backgrounds: Plain, Blur or Cover, with variant-specific washes.
- Music: two-line title area, artist and album, Line / Arc seek progress, elapsed / total time,
  mode control and a 64 px floating D! rendered using the real logo font.
- Double-tap track text to play/pause, swipe right for next or left for previous,
  single tap for track actions/Mood, and the saved Title hold action.
- Launcher: a rising 720 px circular cap, one large icon/label at rest. Items rotate
  about the lower D! hub, drag continuously and snap on release. Tap the item
  to open. Back/outside/D! close. Saved menu order and installed apps are reused.
- QS: eight editable 68 px icon slots, no text captions. Central round artwork, play/pause and a progress ring. Top brightness / bottom battery arcs, with sun
  and battery/charging icons. Radio tap/hold and other item name hints retained.
- Disco list geometry retained. Resting labels abbreviate rather than run
  continuous marquees; the existing library focus behaviour remains available.
- Simple-specific guidance in Help and first-use hints.

## Implementation

`ui/simple_theme.c` / `.h` own the launcher, QS, component kit and explicit
configuration adapter. The adapter remaps inherited `disco_*`, app shortcut and
hint keys only while Simple is active. `th_disco_family()` explicitly opts
Simple into reused Disco layouts; `th_disco()` still means the original preset.

`ui/disco_home.c` retains its established action handlers and popup and applies
Simple-only geometry. `theme.c` appends preset 12 without changing old IDs;
`theme_defs.c` assigns the existing licensed Nunito/Inter faces and Simple kit.
The theme picker includes Simple and its inherited options appear as Simple
Options. Unsupported side and control layouts are hidden
only in Simple.

## Build and render

Apply the supplied patch to commit 9d5e239 (2.4.0 with release notes). This is a
preview, not a replacement release tag. Build normally using the repository's
MIPS toolchain workflow. Native rendering:

```
make -C ui CROSS= host-render
ui/tests/host_build/host-render 12 0 preview-home 0 /absolute/output.ppm
```

The native fixture expects an album image at `/absolute/output.png` before
running; these are deterministic test data, not user library content.
`SIMPLE_BG=0/1/2`, `SIMPLE_NAV=1`, `SIMPLE_POPUP=1` select previews.
Use `preview-quick`, `preview-settings`, `preview-library`, `preview-wifi`,
`preview-bands`, `preview-discoopts` for the other supplied renders.

## Checks and status

Native build, theme-role guard, whitespace checks and focused interaction
checks pass. The focused check covers eight-slot QS save/order, separate Disco
preferences, launcher drag/snap, selection and Back. Supplied images are actual
LVGL output at 360x360, composited onto a review sheet without repainting.
Physical device interaction and animation performance still need approval on
hardware. No existing release binary or GitHub tag is replaced by this preview.

## Preview 0.2 — rising planet

- Nunito Bold track / artist / album: 36 / 30 / 26 px. Music-only alignment uses
  Simple's own saved key; the whole three-line block follows Left / Centred.
- The launcher is now a 720 px circle at x=-180. It rises from y=360 to y=108:
  70% of viewport height above the bottom. Its centre remains below the viewport,
  so only a broad circular cap is visible. Open/close slide in 230/170 ms.
- One solid 80 px icon rotates around the bottom hub. Large icons are generated
  at their real size, with the actual logo face retained for D!.
- Main progress offers Line or Arc. The arc is 270 degrees, starting at 135 and
  ending at 45 degrees; the bottom 90-degree gap is clear for D!.
- QS clock and previous/next controls are removed for this theme. Eight 68 px
  targets with 36 px icons sit around circular artwork. The cover has play/pause
  and its own progress ring; the brightness and battery arcs keep icon labels.
- The large metadata field keeps tap/hold, double-tap play/pause and horizontal
  track swipes. A brief upward arrow comes from the D! position after selections
  and playback actions. This is action feedback, not a new confirmation step.

This pass provides native stills and motion previews, not an update package.

## Preview 0.3 — visible neighbours

- Previous and next launcher icons stay partially visible at the left and right
  edges, rotating continuously into the central position. Side labels fade out.
- Music title / artist / album increase to 38 / 32 / 28 px; both alignments retain
  the same progress bar position and the gesture field covers the full info block.
- Native-size list, USB and clipboard symbols avoid small fallback glyphs.
- The arrow from D! also acknowledges double-tap playback and both track swipes.

## Preview 0.4 — secondary screens and control polish

Simple's component kit owns its control surfaces and ink rather than applying
Ring's solid-fill assumptions first. Buttons retain each screen's geometry;
small keyboard keys are not expanded into neighbouring targets. Round icon
surfaces use a quiet vertical accent wash and a small downward shadow. Orange
icons and selected-state radio shading stay intact through accent restyling.
Large scrolling panels are excluded from the button treatment.

- Search: inset query field, separated keyboard targets, readable active Search
  text, themed results and preserved explicit search / Back behaviour.
- A–Z: opaque palette canvas, orange heading, larger letters and round outlined
  targets. Letter jumping and tap-outside dismissal use the shared callbacks.
- EQ: controls centred without the Disco side-tab offset; On / Off ink remains
  readable on a shaded surface. Existing profile / editor actions are retained.
- App landing buttons, track-popup actions, dialogs and list controls use the
  same Simple surfaces. Stats retains its charts and data presentation, with
  Simple typography, semantic colours and shared control treatment.
- Standby uses Simple's Plain / Blur / Cover selection, palette-aware dimming
  and larger Nunito metadata. Disco's standby path remains unchanged.
- Long screen headers use the smaller Nunito heading face when needed instead
  of clipping the title. Secondary screen headings use the orange accent.

Native review set: Stats overview / top artists, Search keyboard / results,
A–Z in both palettes; Music launcher, QS, Library, EQ overview / faders,
Bluetooth, Wi-Fi, track info, Settings, Battery, Weather, Help, standby,
Roulette / Stats landings, Simple options, Queue, Up Next, Modes, Shortcuts,
Network, System, Files, Books, date/time, Lyrics, volume and track popup.
Empty content fixtures establish chrome and layout; they do not validate live
network, files, lyrics or device state. Standalone installed app internals are
outside the host theme contract.

Focused native checks cover A–Z open / jump / close, typed Search ink and key
separation, Search results / Back, EQ selection / editing / restore, and the
existing Simple QS / navigation / playback gesture checks. Null's typed Search
ink regression check also passes. Device performance and hardware behaviour
still need a physical Disc. This preview includes source and renders, not an
update binary.

## Preview 0.5 — performance and carryover audit

Performance changes are scoped to Simple. This is a source/native preview;
no physical-device FPS, power or touch-latency improvement is claimed yet.

- Launcher icon/vector/label content is rebound only when the catalogue index
  changes. Position and opacity still animate continuously. The native check
  holds the same three bindings across 40 motion frames.
- Hidden QS pauses its timer and ignores presentation refresh requests. Visible
  QS updates changed playback/progress state, checks radios at most once per
  second, and animates charging with a smooth triangular pulse.
- Music and QS artwork refresh only when the artwork revision/source changes.
  Music no longer reapplies its whole layout on repeated artwork publications.
  Title measurement and album text updates follow changed metadata. Standby
  avoids repeated unchanged artwork/progress work.
- Brightness uses the hardware range 1–40, previews through the shared transient
  backlight function, and persists on release / lost press, matching Disco.

Bugs corrected:

- Title hold None no longer toggles favourites; Immersive remains available.
- The metadata gesture bounds follow the displayed field; the linear seek hit
  band retains priority where the album field meets the progress control.
- Leaving Music cancels launcher motion, clears press state and resets its
  closed position, so the next open rises correctly.
- Simple now dismisses old onboarding hints on screen changes, reports launcher
  state to the hint engine, and resets Search's landing on fresh entry.
- Charging opacity resets on unplug; low and unknown battery states are distinct.
- QS mode glyphs follow the live source; drive/volume glyphs are included at
  native icon sizes. Simple-owned colours survive accent restyling.
- The editor correctly says eight QS items, has larger edit targets, and does
  not inherit Disco's artwork backdrop. Long secondary headings fit the round
  viewport. The inherited artist-scroll restart no longer overrides Simple's
  single-line artist presentation.
- Background restart failure restores the background key, not theme_preset.

Carryover validation uses the real shared QS editor (reorder, add/remove,
empty selection, reopen and configuration isolation) in Simple, Disco and
Null. Simple checks include Line/Arc playback gestures, Title hold None and
Immersive, track popup Back and edge-swipe, logo shortcut, fresh Search landing,
and playback-picker / Immersive return routes with animations on/off.
Brightness range/save timing and charging/unplug/low/unknown state are checked
in the native harness; missing host backlight sysfs nodes are expected.

Additional native review covers the QS editor, password keyboard, EQ band
editing, progress selector, unsupported-codec explanation, Music Assistant and
SongSphere landing/tag pages. SongSphere's mood colour field is intentional
application content. Installed standalone app internals remain outside scope.
Hardware radio transitions, actual brightness, animation performance, real media
and custom app handoff still need a physical Disc.
