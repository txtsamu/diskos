# Squash — 2.5.0

A separate, opt-in theme, featured in diskOS Disco! 2.5.0. Existing presets keep their IDs,
configuration keys and presentation paths. Squash uses Disco's menus, track
popup, editor and system actions rather than duplicating their behaviour.

## Start here: big, bold, touch-first 🎃

Choose Display → Theme → Squash. Double-tap Music text for play/pause, swipe right/left to skip, single-tap for actions/Mood. D! opens a rising launcher; drag, release to snap, and tap the icon to open. The configured Hold works on the logo and large icon. Pull down for eight editable Quick Settings.

**Settings → Help**, below Battery: How to has one topic per page; first-use hints appear in context; Show hints again resets learning. Squash stores hint preferences separately from Disco and Null. Volume now uses the clean bottom arc and D! dismissal, with no ± buttons or whole-screen dimmer. It auto-hides after 1.8 seconds idle, paused while dragging.

The sections labelled Preview/2.4.0 below record design history; the 2.5.0 summary and final refinement/Volume B sections describe the shipped configuration. The permanent dots/arrows from earlier previews are removed. Nunito Bold defaults; all three Track Font options use fixed 36/32/28 px roles.

## Presentation

- Dark charcoal / off-white; orange emphasis and outlined round controls.
- Nunito Bold 700 at 22/30 px for general important text and 36/32/28 px for Music; Inter Medium for smaller
  detail, with existing international and symbol fallback chains.
- Music backgrounds: Plain, Blur or Cover, with variant-specific washes.
- Music: fixed-size title, artist and album, Line / Arc seek progress, elapsed / total time,
  read-only playback/mode indicators and a 64 px floating D! rendered using the real logo font.
- Double-tap track text to play/pause, swipe right for next or left for previous,
  single tap for track actions/Mood, and the saved Title hold action.
- Launcher: a rising 720 px circular cap, one large icon/label at rest. Items rotate
  about the lower D! hub, drag continuously and snap on release. Tap the item
  to open. Back/outside/D! close. Saved menu order and installed apps are reused.
- QS: eight editable 68 px icon slots, no text captions. Central round artwork, play/pause and a progress ring. Top brightness / bottom battery arcs, with sun
  and battery/charging icons. Radio tap/hold and other item name hints retained.
- Disco list geometry retained. Resting labels abbreviate rather than run
  continuous marquees; the existing library focus behaviour remains available.
- Squash-specific guidance in Help and first-use hints.

## Implementation

`ui/simple_theme.c` / `.h` own the launcher, QS, component kit and explicit
configuration adapter. The adapter remaps inherited `disco_*`, app shortcut and
hint keys only while Squash is active. `th_disco_family()` explicitly opts
Squash into reused Disco layouts; `th_disco()` still means the original preset.

`ui/disco_home.c` retains its established action handlers and popup and applies
Squash-only geometry. `theme.c` appends preset 12 without changing old IDs;
`theme_defs.c` assigns the existing licensed Nunito/Inter faces and Squash kit.
The theme picker includes Squash and its inherited options appear as Squash
Options. Unsupported side and control layouts are hidden
only in Squash.

## Build and render

Apply the cumulative patch to tag v2.3.0 (005440795943c2af8865cc8e4b6a0f40e50febf9).
The package includes the compiled 2.5.0 mq_ui and full matching source; no remote
release tag is created by this package. Build normally using the repository's
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
  Squash's own saved key; the whole three-line block follows Left / Centred.
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

Squash's component kit owns its control surfaces and ink rather than applying
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
  same Squash surfaces. Stats retains its charts and data presentation, with
  Squash typography, semantic colours and shared control treatment.
- Standby uses Squash's Plain / Blur / Cover selection, palette-aware dimming
  and larger Nunito metadata. Disco's standby path remains unchanged.
- Long screen headers use the smaller Nunito heading face when needed instead
  of clipping the title. Secondary screen headings use the orange accent.

Native review set: Stats overview / top artists, Search keyboard / results,
A–Z in both palettes; Music launcher, QS, Library, EQ overview / faders,
Bluetooth, Wi-Fi, track info, Settings, Battery, Weather, Help, standby,
Roulette / Stats landings, Squash options, Queue, Up Next, Modes, Shortcuts,
Network, System, Files, Books, date/time, Lyrics, volume and track popup.
Empty content fixtures establish chrome and layout; they do not validate live
network, files, lyrics or device state. Standalone installed app internals are
outside the host theme contract.

Focused native checks cover A–Z open / jump / close, typed Search ink and key
separation, Search results / Back, EQ selection / editing / restore, and the
existing Squash QS / navigation / playback gesture checks. Null's typed Search
ink regression check also passes. Device performance and hardware behaviour
still need a physical Disc. That historical preview included source and renders only; the final 2.4.0 package includes mq_ui.

## Preview 0.5 — performance and carryover audit

Performance changes are scoped to Squash. This is a source/native preview;
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
- Squash now dismisses old onboarding hints on screen changes, reports launcher
  state to the hint engine, and resets Search's landing on fresh entry.
- Charging opacity resets on unplug; low and unknown battery states are distinct.
- QS mode glyphs follow the live source; drive/volume glyphs are included at
  native icon sizes. Squash-owned colours survive accent restyling.
- The editor correctly says eight QS items, has larger edit targets, and does
  not inherit Disco's artwork backdrop. Long secondary headings fit the round
  viewport. The inherited artist-scroll restart no longer overrides Squash's
  single-line artist presentation.
- Background restart failure restores the background key, not theme_preset.

Carryover validation uses the real shared QS editor (reorder, add/remove,
empty selection, reopen and configuration isolation) in Squash, Disco and
Null. Squash checks include Line/Arc playback gestures, Title hold None and
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

## Preview 0.6 — motion, playback and backgrounds

1. Squash page push/pop runs in 180/140 ms, the main launcher opens/closes in
   180/130 ms and snaps in 110 ms. The display refresh timer targets 16 ms while
   this theme is active. Immersive enters/exits in 400/180 ms. The animation
   budget allows the three metadata marquees alongside page motion.
2. Active QS radios, EQ, non-default source/playback modes and rescan state use
   a solid orange surface with contrasting ink. Outlines remain visible. The
   playing cover badge and Squash EQ On control use the same active treatment.
3. Music metadata uses Nunito Bold 32/28/26 px. Overflowing title, artist and
   album run exactly three cycles on track/metadata identity changes, then rest
   at the start. Repeated position frames do not restart them. Long album names
   use a full-length metadata cache and are restored before marquee setup.
4. Play/pause and previous/next feedback fly from D! with the corresponding
   glyph. Playback state comes from the UI's play-state source for Squash;
   unreliable raw metadata is not used to choose its glyph.
5. A shared Play mode QS item (code p) cycles Sequential, Shuffle, Repeat One,
   Repeat All and Single. Existing configured lists keep their saved entries.
   It can be selected in every theme. Older fixed-layout presets keep their six
   positions and now expose Quick items under Display > Text & Layout; Disco
   retains seven slots, Null/Squash eight. Legacy selections use qs_items,
   separate from Disco, Null and Squash preferences.
6. The playback-mode control is hidden on Squash's Music screen. Track actions
   retain their existing mode control and Play mode can be selected for QS.
7. Header Back circles are 36 px; the arrow size remains unchanged and the
   expanded hit area keeps them comfortable to press.
8. A fixed pair of clipped gradient strips gently fades launcher side icons
   into the theme canvas. No per-frame blur or bitmap processing is involved.
9. Squash's cover/blur wash is about 30% (77/255) in both appearances and standby.
10. Squash backgrounds use a moderate blur at 120x120, then bilinear upscale to
    360x360. Filter kernels are 5/7/7, using integer arithmetic in the artwork
    worker. This is softer than a cover without the former heavy sigma-19 blur.
    The worker skips an unused Disco tint allocation/pass for Squash.

Checks: real QS button cycles and editor access pass across all thirteen
presets. Squash's metadata animations are checked for three repeats, stopping,
and restarting for a new identity; action-specific feedback glyphs are checked.
Existing navigation, brightness/battery, popup, Immersive return and shared QS
editor parity checks pass. Decoder comparison preserves old art outputs
byte-for-byte, and Squash cover/thumbnail output remains unchanged. Only its
background filter differs. Native timing is informational; device FPS/power
still need hardware measurement.

## 2.4.0 final refinement

Music metadata uses fixed Nunito Bold 36/32/28 px (title/artist/album), shared with standby metadata. Read-only playback and mode indicators flank the time cells. Gesture hints disappear after ten seconds without touch. Menu items and Quick settings lead every Disco-family theme options list; Music and Settings remain protected. Squash retains preset 12 and the existing `simple_*` configuration keys for upgrade compatibility.

The configured Hold shortcut works on both the large launcher icon and the D! logo. It fires once per hold, ignores dragging, and Nothing leaves the launcher open. Double tap retains its own action; both shortcuts share validation and legacy action migration.

## 2.4.0 refinement update

All three Music font options use fixed title/artist/album sizes of 36/32/28 px. Nunito Bold is the default; Squash stores Track Font in `simple_track_font`, independently of Disco. Original uses Montserrat, and Inter Semibold has dedicated rasterized faces at the same sizes. Standby metadata shares these roles.

The D! circle is 72 px, centred at (180,316), with a larger centred logo and matching feedback origin/hit area. Read-only play/pause and playback-mode glyphs now sit left/right of the logo, separately controlled by Playback state and Playback mode switches in Music Screen. They remain independent of Track Times and the progress style. Gesture dots/arrows are removed. The Track Times toggle only reveals the foreground labels, fixing duplicate shadow text. Blur uses a 20% overlay on Music and standby; Cover retains 30%.

## Volume flyout B

Squash volume now opens as a bottom circular cap concentric with the main launcher. A single orange arc controls the full device range, 0–120; the live value is above the same 72 px D! hub. Endpoint labels are read-only and there are no plus/minus buttons. The rest of the screen stays undimmed. Tap D! or outside to close; the flyout also closes after 1.8 seconds without input, but remains open while dragging. Entry/exit animations take 140/110 ms. The retained overlay allocates once and pauses its timer when hidden.

Drag writes are limited to one per 120 ms, with the final deferred value committed on release or press-lost. Short-lived player echoes cannot pull the slider away from a drag. Active overlay touches are owned by Squash, so they cannot operate the Music gestures underneath. Null and other themes retain their existing volume overlays.
