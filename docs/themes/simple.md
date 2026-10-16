# Simple — theme preview 0.3

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
