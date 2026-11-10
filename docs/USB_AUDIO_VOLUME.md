# USB Audio volume and the Disc's volume keys

While the Working Mode is Local and the Output route is USB Audio (an external USB DAC or IEM cable), diskOS applies the
volume on the DAC's own ALSA playback volume control and holds the Player at unity, so the samples pass untouched. This is
the `dacvol` module (`ui/dacvol.c`). If the DAC has no usable volume control, or `amixer` fails, the Player volume is used
as before.

## What the volume keys do on USB Audio

The Player decides per gesture what a volume key does. Settings > Controls stores three choices, each "Adjust Volume" or
"Switch Track": single click, double click and long press (`key_single`, `key_double`, `key_long`; the Player reads them as
`KEY_SINGLE_CLICK_SLE`, `KEY_DOUBLE_CLICK_SLE`, `KEY_LONG_PRESS_SLE`).

| Gesture | Key event | On USB Audio |
|---|---|---|
| Single click up / down | 251 / 252 | Volume step on the DAC, when the single-click action is Adjust Volume |
| Double click | 266 / 267 | Left to the Player (next / previous track); a trailing 251/252 within 700 ms is ignored |
| Long press | 262 / 263, auto-repeating | Not handled by diskOS; see below |

A double click set to Switch Track no longer disables the volume keys. Before 2026-11-10 the module required all three
gestures to be Adjust Volume, so with Double click = Switch Track, a common setting, single clicks did nothing on USB Audio.

## Limits and what is not verified

- The codes come from raw reads of `/dev/input/event0` (`x2000_key`) on a Disc running V2.57. 251/252 as up/down is from the
  original `dacvol` change; 266/267 as the double-click events and 262/263 as the long press are inferred from captures where
  the presses were not labelled. Which of 266/267 is next and which is previous, and which of 262/263 is up, is unverified.
- A long press that the Player turns into a volume change still reaches the DAC (the module notices the Player volume moving
  and applies the difference), but because the Player is held at its maximum it can only go down. Long-press up is not
  handled.
- A double click can also produce a 251/252 just before the 266/267; that would cost one volume step.
- The volume path is unchanged for Local Playback: the Player handles its own keys there.
