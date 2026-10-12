# UI choices

[Back to the README](../README.md)

Why this fork looks and behaves the way it does — including the bits that aren't obvious from the screen.
The Disc is a **360 × 360 round display**, a single-core-feeling MIPS chip and a finger. Most decisions
fall out of those three facts.

---

## Themes

### One theme = the whole interface
A theme is not a colour scheme. Picking **Ring** or **Braun** changes Home, Now Playing, the screensaver,
popups, toasts, the equalizer and every list together. Switching restarts the UI; that's deliberate —
it keeps the running interface simple and fast instead of re-skinning live.

### Ring
- **The progress ring is the interface.** Home puts the clock inside the playing track's ring, with the
  cover riding along it. Now Playing wraps the cover in the same ring — **drag the ring to seek**.
- **Two colours, two jobs.** The *interface accent* (red on Ring, lime `#A6E22E` on Ring Light) marks
  things you can press: every real button gets a 2 px accent ring. The *album colour* is for the music —
  glyphs, play/pause, title. You can always tell "control" from "content".
- **Heart left, play mode right**, both on the ring's horizontal apexes; queue below play mode.
- **Bigger type:** title 28 px, artist 20 px — matched to Braun so the two themes feel equally legible.
- **Tap the cover** → immersive record. **Tap the title/artist** → the album, with the current track focused.

### Braun
- Borrowed from the Braun **SK4** radio: warm white body, speaker-grille dot field above, a solid panel
  following the round edge below, **one orange** for focus and "on".
- **ON/OFF knobs instead of lamps.** A toggle is a knob: pointer straight up in the icon colour = off,
  rotated **45° and orange** = on. No indicator lamp — the pointer *is* the state, like a real dial.
- **Lists stay straight** (unlike Ring's curves) — it's a radio, not a watch.
- **Braun Dark** (charcoal body, aluminium knobs) and an **Automatic Appearance** toggle for day/night.
  The volume popup uses the theme's own surfaces in Dark, so it no longer flashes a light band at night.

### Hidden upstream themes
Upstream ships eight presets. The theme list shows **Ring, Braun, Stone** only. The others are still
compiled in and keep working if they were already selected — they're hidden, not removed, so nothing
breaks after an update.

---

## Navigation

| Gesture | Does |
|---|---|
| Swipe up from the **bottom edge** | Home, from anywhere (bottom 40 px, twice the normal travel — hard to trigger by accident) |
| Pull down from the top | Quick Settings orbit |
| **Hold** an album / artist / genre / folder | Actions: Play · Shuffle · Add to queue · Playlist |
| Tap the album art on Now Playing | Immersive record |

**Why long-press opens a menu instead of playing:** on a round screen a mistimed hold is easy, and
silently replacing your queue is the worst outcome. A menu costs one tap and is never destructive.
The lists carry a small hint: "Hold an item for more".

**Curved lists.** Library lists (Songs, Albums, Artists, Genres) follow the screen's curve like the
History list. Rows near the top and bottom step inwards so text never runs under the bezel. The A–Z
button sits on the right edge so it doesn't push the list off-centre.

**Left-handed mode mirrors places, not reading.** Side = Left moves the navigation circle and every screen
arranged around it to the left, and the back swipe to the right edge, so the hand that holds the Disc can reach
the circle. Text keeps reading left to right and arcs keep their direction: progress is clockwise for
everyone. The change restarts the interface (Apply), so every screen is rebuilt in its new place.

**Three scroll styles.** Classic narrows rows symmetrically with the round screen. Straight keeps them flat
for people who find curves busy. Disco curves only the edge opposite the circle, as if the rows were spokes of
a CD turning round its hub, and keeps the times in one straight column so they stay easy to compare.

**Settings in short pages.** Display and System had about twenty rows each, with four in view. They are now
split into five or six topic pages each, so the most scrolling you ever do is a little.

**A welcome when nothing plays.** An empty Music screen gave no hint what to do. It now shows a generated CD
and "Go to Library", which is where you'd go next anyway.

**The EQ dial owns its touches.** While your finger is on the dial, page swipes are ignored — no more
leaving the equalizer by accident while turning a band.

---

## Queue

The stock player doesn't refresh a loaded queue when songs are added, so **Add to queue rebuilds it**.
Queued songs play **first-in, first-out** after the current song; shuffle and repeat pause while they play, then
whatever you were listening to resumes. There is no separate "Play next" — one rule, easy to predict.

---

## Covers and performance

### Covers decoded in-process
Upstream decodes artwork in a short-lived helper (`diskos-artdec`) with a safety lock. After quick skips
or album hopping the lock could leave the helper un-started, and covers stopped for good. This fork
**decodes inside `mq_ui`** (`ui/artdec_inproc.c`), with per-run state reset and allocation tracking so a
broken image can't leak or wedge it. The helper binary is still shipped for compatibility; the UI doesn't run it.

### Immersive record
- Paced on the **whole** frame time (render + compose + flush), so it spins at a steady rate instead of
  stalling at 1 fps.
- Lyrics, hint and progress ring are **baked into the frame** from snapshots taken only when they change,
  instead of being redrawn on every rotation.
- The rotation loop has no per-pixel bounds tests (same output, fewer instructions).
- **Nothing is drawn while the screen is dimmed or off.**

### Theme pass skipping
The theme kit restyles new objects on each frame. A counter in the vendored LVGL (`lv_obj_class.c`)
lets it skip frames where nothing was created and the screen and accent haven't changed — a cheap win
on every screen. *If you update LVGL, carry this one-line patch over.*

---

## Power and screen

- **Hold the power key** → themed "Shutting down / Saving…" screen while the player flushes.
  The UI reads the key directly (GPIO via `/dev/mem`) because the stock event never reaches it in time.
- **Brightness** is restored after screen off → on (stock reset it to default).

---

## Updates

Upstream's Wi-Fi updates trust upstream's release key. This fork **removes that key** and bakes in
**yours**, so the Disc accepts only builds you signed — via the SD card. Why the SD card and not SSH:
the boot script verifies `mq_ui` against a read-only manifest, so hand-copying a binary gets quarantined
and the Disc falls back to stock. Signed updates go through the proper trial-and-rollback path instead.
See [UPDATING.md](UPDATING.md).

---

## On hold

- **Glass** — a third theme with a navigation hub (Music · Library · Settings · Modes · Shortcuts · EQ),
  configurable hub items and a title-tap action overlay. Mocked up, not built.
- **Null** (Nothing-phone pixel style) and a 90s cassette theme — shelved after testing.

## Disco 1.3.1: readable, tactile, economical

Song Info is an album back sleeve: a cover stamp and a calm metadata card. Music's title menu
opens Info; immersive art remains a title-hold/shortcut action. Queue gives its space to the
highlighted current track and queued rows, separated by a line, with larger controls and no
small explanatory copy. Clear keeps its two-tap confirmation.

Action menus and Shortcuts use larger touch targets and labels. Pressed Quick Settings,
Modes and shortcut controls change their existing fill and border, without a continuous
animation. The EQ spans the available width and gives its preset and band labels more room.

Original remains the default Music font; Inter Semibold and Nunito Bold are optional heavier
bitmap faces. Existing international/CJK fallback chains remain. Library position memory is
bounded to twelve views in RAM; position hints reuse existing timing. These choices avoid
runtime font rasterization, persistent scroll writes and new idle timers. Battery benefit is
not a measured claim. See [1.3.1](releases/v1.3.1.md) for the full release.

## Disco navigation gestures (1.3.2-polish1 candidate)

Settings > System > Controls adds **Double tap** and **Hold**. Choose with the
left/right arrows, then tap **Apply**; Back discards an unconfirmed choice.
Both offer Music screen, Search, Queue, Up Next, Shortcuts, Quick Settings,
and Nothing. The defaults are Double tap = Search and Hold = Music screen.

These gestures operate only in the **open** Disco navigation menu, identically
in Right and Left layouts and across navigation sections. The closed shard
retains tap-to-open behaviour. Section arrows and swipes remain immediate.
A centre-icon tap waits 260 ms for a second tap; selecting Nothing for Double
tap removes that delay. A hold consumes its release, and closing the menu or
changing screens cancels a pending tap. Gesture actions close the navigation
menu before opening their destination.

Recognition uses one temporary timer per pending tap, with no idle polling or
new animations. Real LVGL pointer tests cover both sides, all seven actions,
hold/release, closed-shard behaviour, cancellation and Apply/Back. Device
testing remains required. Stable release assets remain at 1.3.1.

## First-use hints (1.3.2-polish2 candidate)

Disco offers contextual tips for opening navigation, using its arrows/icon,
holding Library songs/albums for actions, and tapping the Music title. A small
plain pill appears after an 800 ms pause and lasts four seconds. Only one is
visible; touching the interface or changing screens dismisses it. Tapping the
menu hint itself opens navigation. It mirrors with the shard; the other hints
use shared positions that clear navigation buttons and Music transport/seek.

Settings > System > Help > First-use hints offers:

- **Automatic** (default): each tip retires after successful discovery. Opening
  the menu and navigating sections each require two successful uses; Library
  actions and the Music title overlay require one each. Automatic tips also
  stop after five healthy Disco UI starts. A start counts only after 30 seconds
  of event-loop uptime, so immediate crashes/restarts do not consume it.
- **On**: tips remain available regardless of discovery/start counts, with
  each tip shown at most once per UI session.
- **Off**: contextual tips are hidden.

**Show hints again** resets only hint state and returns to Automatic; unrelated
preferences remain. Existing users who already discovered the old navigation
hint retain that discovery until reset. Library tips are offered only for
nonempty actionable views, not category/empty lists. Custom Hold/Double tap
actions continue to be explained in their Controls settings.

No blur, pulsing, transitions, or permanent hint polling is added. Counters use
existing atomic configuration writes at discovery/start events, not timer ticks.
Device testing remains pending. The compiled candidate is separate from the
stable 1.3.1 payload.

## Disco Quick Settings (1.3.2-polish4 development candidate)

Display > Disco Options > Quick Settings offers Style (Current / Arc) and Items.
Arc is the default when no style has been saved. Existing saved styles remain.
The approved Arc layout has eight 66px circular targets: six evenly spaced on a
116px radius, concentric with the battery/brightness rim, and two inner targets
between that arc and the music disc. Left mirrors the complete layout.
The music disc, progress, playback and rim controls keep their existing geometry.

Items uses the Disco Menu editor to add, remove and reorder up to eight distinct
choices, with immediate persistence. Wi-Fi and Bluetooth reserve the two inner
positions when selected. Other actions fill the outer arc in editor order; if a
radio is omitted, its inner position can hold an additional action after the six
outer positions. Fewer items and an empty selection are supported.

Default items: Wi-Fi, Bluetooth, Modes, Library, Equalizer, Search, Rescan,
Settings. Also available: Queue, Up Next, Shortcuts, Weather, Lyrics, Song Info,
Immersive, Folders and Favourites. Existing saved item lists remain intact.
Wi-Fi/BT always show live On/Off state; Bluetooth startup shows pending dots.
Hold a radio for its settings; hold another button for its action name.
Rescan retains confirmation. Press feedback uses simple fill/border changes.

Current retains its row design for six or fewer items and uses compact rows for
seven/eight, preserving every selected action when switching styles. Changes
rebuild only controls, preserving playback and the disc. No new periodic timer
or expensive animation is added. Device testing is pending; this is dev work,
not a stable release.

## Current QS row polish: 1.3.2-polish5

Current style locks Wi-Fi/BT to their original top pills by identity, independent
of editor order. Remaining actions share the 224px row field equally, with at
least 4px between targets and a 50px height cap for sparse selections. Both side
layouts mirror correctly. Bold Inter captions fit on one line, stepping through
22/18/16px as needed; icons shrink for an eight-action list without radios.
Captions: MODE, SCAN, SONGS, EQ, FIND, SET, QUEUE, NEXT, SHORT, WEATH, LYRICS,
INFO, COVER, FILES, FAVS. Hold still shows the full action name. Arc is unchanged.
Native checks exercise every action at the narrow bottom row, equal heights,
gaps, unwrapped/unclipped text, no-radio eight-item layout, both sides and radio
state/input/editor persistence. Theme/punctuation checks pass. Device test pending.

## QS curvature follow-up: 1.3.2-polish6

Current rows follow the outer bezel and rounded music/navigation field instead
of ending at one straight edge. Captions align left along the curve, with extra
icon/text clearance. Below the field the final pill returns to the screen centre,
matching the original Settings row. Equal row heights/gaps and bold single-line
labels remain. Radio positions remain unchanged in both styles.

Arc's six outer target centres move from 116px to 128px radius, concentric with
the battery/brightness arcs. Targets remain 66px with equal angular spacing;
Wi-Fi/BT keep their inner positions. Both sides, text fit, circular bounds,
non-overlap, curve/radius, item editing and radio controls pass native LVGL checks.
Device performance/acceptance is still pending.

## QS spacing follow-up: 1.3.2-polish7

Current pills share a compact width measured from the widest catalog abbreviation
at the actual bold font size, with 12px trailing padding. Curved positioning is
retained and the final pill stays centred below the navigation field. The gap
below fixed Wi-Fi/BT equals all action inter-row gaps: 4px in the full selection.
Sparse layouts keep equal integer gaps and capped 50px row heights. No wrapping,
clipping or per-frame measurement is added; text sizing happens on control rebuild.

Arc outer centres move inward from 128px to 122px radius, adding 6px clearance
from the brightness/battery rim while preserving equal angular spacing, 66px
buttons and both fixed radio positions. Both-side native geometry, exact gaps,
uniform widths, all caption fits, radio state/input/editor tests and theme/text
checks pass. Matching static MIPS candidate supplied; owner device test pending.

## Protected edge targets: 1.3.2-polish8

Back starts within 64px of the side edge (ui/main.c). Current QS action targets
now start at least 72px inward: an 8px buffer beyond that gesture zone, mirrored
for Left. Compact pills are at most 116px wide, gently curved, with equal gaps
and single-line bold labels sized to fit. Radio positions and Arc are unchanged.

Disco Modes retains 56px row heights and existing vertical positions, shortens
caps by 12px at each end, and enforces the same protected edge. Icons/labels/check
indicators keep separate space; label fonts step down only where necessary to
fit. Mode switching and back gesture handling are unchanged. Native both-side
checks verify protected bounds, caption fits, row heights and QS interactions.
Static MIPS build supplied; owner physical-device testing is pending.

## Replacement 1.4.0

QS List/Modes bow toward the free edge, with consistent 22px labels and 4px protection beyond the 64px Back strip. QS radios stay fixed and Arc style is unchanged. Disco is first in the theme list; Progress Shape belongs to Layout. The Battery gauge opens around the navigation side, mirrors for Left, and layers its total-pill backgrounds below the arc.

EQ and album-colour database work must not run on the UI thread. Large artwork preparation is optional when memory is constrained. Swipe motion cancels QS/mode button actions. These changes preserve theme roles and do not add continuously running animations.
