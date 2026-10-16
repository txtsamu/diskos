# diskOS Disco 2.4.0 — device-test candidate

Starting point: cumulative 2.3.9 (6821e7c). The full patch is against v2.3.0.
Includes the complete Null theme and all cumulative 2.3.9 refinements.

## Final fixes

1. Centre option selectors vertically on every shared settings detail screen.
   Enlarge option text and help text; keep controls and scrollable help separate.
2. Null stays right-handed. Retain Side, mark it Disabled, explain why, and
   ignore saved left-handed geometry without deleting the preference.
3. Disco/Null How to: one heading per snapped page, larger type, bottom next-page
   arrow. Null A-Z button and letter overlay use a 50% black background.
4. Library song, album, artist and playlist names use abbreviated resting labels.
   Only the focused row (nearest the screen centre, or keyboard focused) scrolls.
5. Disco standby has a larger clock, weather and track text, with status at the
   top and title/artist grouped around the vertical centre.
6. Long Music metadata scrolls three cycles after a track change and then rests.
7. Upper first-use hint popups move to the centre, with larger text.
8. Null handle hint names the configured double-tap and hold shortcuts, plus
   dragging and continuous scrolling at either end.
9. Null QS battery and brightness receive separate 30% black side strips;
   the centre artwork/controls area is unchanged.

## Null: a complete second theme

Null is second in the theme picker and website theme list, after Disco. It uses
pixel typography, two vivid palettes, three artwork/background treatments,
custom transport, smooth sliding navigation and a vertical Quick Settings panel.
QS offers text or Disco's shared icons. Four Music font combinations include
Matrix: large dot-matrix titles paired with terminal details.

The polish is practical: larger touch areas, clearer selectors and explanations,
focused-only library scrolling, quieter Music metadata, contextual tooltips and
one-topic How to pages. The website includes native screenshots and a dedicated
Null section explaining appearance, navigation, quick settings and guidance.

## Validation and limits

Focused native renders cover selectors, disabled Null Side, How to pages, Music,
Quick Settings and Disco standby. Focus/metadata animation checks and the shared
theme guard run before packaging. The device build verifies static MIPS32r2,
FP64 and NaN2008. The binary patch must reconstruct the packaged Git tree exactly.
Physical Disc testing remains pending; no website publication is performed.

## Shared Help menu

Disco and Null both place Help directly below Battery in Settings. Its order is
How to, First-use hints, Show hints again. Null no longer duplicates these in
Null Options. Each theme retains its own saved hint mode and learning counters.
Back from How to returns to Help; Back from Help returns to Settings.
