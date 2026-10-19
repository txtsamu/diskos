# Immersive controls — 2.5.4

Open **Display → Music screen → Immersive**. **Circular control** defaults Off; turn it On and choose **Control action: Duration / Volume**.

Rotate the artwork clockwise to increase the selected value or counterclockwise to decrease it. The picture follows your finger, including while paused, and normal rotation resumes from the released angle. Crossing the centre re-anchors without a value jump. Sensitivity is 75% of the earlier dev2 candidate.

**Duration:** a compact box shows the selected time and total. Lyrics hold while dragging; release commits one seek and resumes lyrics at the new position. Changed tracks and lost touches cancel the pending seek. Old player position echoes are briefly ignored after an accepted seek.

**Volume:** the existing themed volume overlay shows changes. Writes are throttled to one per 120 ms plus a final release value when needed, and the configured maximum is respected. Lyrics continue. The overlay is click-through during this mode so it cannot interrupt artwork dragging; normal volume controls return when leaving Immersive.

With Circular control On, use the **left-edge Back swipe**, with themed feedback, to leave. There is no permanent arrow and tapping the cover does not exit. With it Off, the original tap-to-exit behavior remains.

**Styles:** Vinyl, CD and Cover all rotate. Cover is undecorated artwork with optional lyrics and replaces the former None setting without changing its saved value. New lyric lines slide upward over 240 ms. Text snapshots are reused during animation; no text images are allocated every frame.

Null also supports hold-and-drag progress seeking on its normal Music screen, with a time preview and one seek on release.

Native builds, LVGL renders and focused checks do not replace extended physical-device gesture and battery testing. No measured battery-life improvement is claimed.
