# Simple — theme preview 0.1

A separate, opt-in theme on the 2.4.0 baseline. Existing presets keep their IDs,
configuration keys and presentation paths. Simple uses Disco's menus, track
popup, editor and system actions rather than duplicating their behaviour.

## Presentation

- Dark charcoal / off-white; orange emphasis and outlined round controls.
- Nunito Bold 700 at 22/30 px for important text; Inter Medium for smaller
  detail, with existing international and symbol fallback chains.
- Music backgrounds: Plain, Blur or Cover, with variant-specific washes.
- Music: two-line title area, artist and album, seek line, elapsed / total time,
  mode control and a 64 px floating D! rendered using the real logo font.
- Double-tap track text to play/pause, swipe right for next or left for previous,
  single tap for track actions/Mood, and the saved Title hold action.
- Launcher: a central 300 px circle, one large icon/label at rest. Items rotate
  about the lower D! hub, drag continuously and snap on release. Tap the item
  to open. Back/outside/D! close. Saved menu order and installed apps are reused.
- QS: eight editable 56 px icon slots, no text captions. Dedicated play/pause,
  previous and next controls. Top brightness / bottom battery arcs, with sun
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
Options. Unsupported alternative navigation shapes/control layouts are hidden
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
