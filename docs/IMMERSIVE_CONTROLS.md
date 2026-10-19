# 2.5.4-dev2 controls

Based on the last 2.5.4-dev1 source bundle (b960fb6).

Display → Music screen → Immersive now has Circular control (default Off) and Control action: Duration / Volume. On replaces tap-to-exit with a Back button and edge back gesture. Rotate the artwork clockwise to increase the selected value, counterclockwise to decrease it. Artwork follows the finger, including while paused, and resumes its normal rotation from the released angle. Centre crossings re-anchor without a value jump.

Duration shows a centred time/total readout, holds the current lyric lines during the drag and seeks on release. Lyrics then refresh at the requested position while briefly rejecting stale position echoes. A changed track or lost press cancels the seek.

Volume uses the existing themed volume overlay and sends changes at most once per 120 ms, plus the final release value if needed. The configured maximum volume is respected. Lyrics continue. The volume display is click-through while this circular control is selected so it does not interrupt the artwork gesture; normal volume input is restored when leaving Immersive.

Null's progress bar shows a time readout on hold, follows a horizontal drag and seeks once on release. Track changes and leaving the screen cancel the pending seek. All four Null Music font sets and list reading sizes increase one step using baked font assets. Roulette's landing description is shortened to fit above its button.

Native font renders and Null hold/drag/release, changed-track and screen-leave tests passed during implementation. The completed Immersive addition has not had an extended native or device test pass; the owner requested immediate binary/source delivery. Test this development build before promotion. Shipping payload and public release remain unchanged.


## Development candidate 3

- Immersive seek feedback fits its time text with 10 px horizontal padding, retaining the existing fonts and total-duration line. Other screens retain their previous feedback dimensions.
- A left-edge swipe replaces the permanent back button, with the standard themed gesture feedback.
- Circular duration and volume control sensitivity is 75% of dev2. Artwork rotation still tracks the finger directly.
- Lyric changes slide upward over 240 ms. Cached text snapshots are reused throughout each slide, with frame-paced drawing. Duration seeking holds lyrics; volume changes do not.
- Cover replaces None at the same stored setting value, with the same rotating and touch-controlled behaviour as Vinyl/CD, without their decorative layers.
- Boomshine: “One spark. Catch them all.” Stats: “See what moves you.”

Native build and actual LVGL feedback render checked. Physical-device gesture, lyric and battery testing remains necessary.
