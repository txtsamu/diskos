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
| Long press (hold) | 262 down / 263 up, auto-repeating about 20 per second | One volume step per event on the DAC, when the long-press action is Adjust Volume |

A double click set to Switch Track no longer disables the volume keys. Before 2026-11-10 the module required all three
gestures to be Adjust Volume, so with Double click = Switch Track, a common setting, single clicks did nothing on USB Audio.

## Limits and what is not verified

- The codes come from raw reads of `/dev/input/event0` (`x2000_key`) on a Disc running V2.57. 251/252 as up/down is from the
  original `dacvol` change. 263 as hold-up and 262 as hold-down were captured with labelled presses on 2026-11-11, and the
  hold was confirmed working on the Disc. 266/267 as the double-click events is inferred; which of them is next and which is
  previous is unverified, and diskOS does not need to know.
- A double click can also produce a 251/252 just before the 266/267; that would cost one volume step.
- The volume path is unchanged for Local Playback: the Player handles its own keys there.
