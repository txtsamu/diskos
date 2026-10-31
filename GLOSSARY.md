# diskOS player UI

The on-device UI that replaces the stock interface on the Disc and drives the stock Player over its command channel. This glossary names what the UI decides about audio: where sound comes from, where it goes, and the steps between.

## Language

**Disc**:
The handheld music player hardware that runs diskOS.
_Avoid_: device, unit

**Player**:
The stock playback firmware process on the Disc that the UI sends commands to. It owns decoding, output and the USB port; the UI only asks.
_Avoid_: engine, daemon, mq_player (a binary name, not the concept)

### Modes and routes

**Source mode**:
What the Disc is currently for: Local playback, USB DAC, Bluetooth Receiving or USB Storage. Exactly one is active.
_Avoid_: input, audio source

**Working Mode**:
The user-facing picker through which a source mode is chosen, and the choice itself that survives a restart.
_Avoid_: mode (alone, ambiguous with Player work mode and play mode)

**Output route**:
Where Local playback's sound goes: the internal DAC, SPDIF, USB Audio, or a Bluetooth sink. Only meaningful while the source mode is Local.
_Avoid_: output device, out mode

**USB Audio**:
An output route in which the Disc drives an external USB DAC. It is not a source mode, even where the picker shows it as a row. The DAC or IEM's own buttons control playback; see **Click gestures**.
_Avoid_: USB DAC (that is a source mode: the Disc acting as a DAC for a phone or computer)

**Click gestures**:
Counting repeated presses of a USB Audio headset's play/pause button: one click plays or pauses, two go to the next track, three to the previous. A sequence ends 350 ms after the last click, or at the third.
_Avoid_: double tap (that is a touchscreen gesture), key press (the Disc's own physical keys are handled by the Player)

**Player work mode**:
The Player's own record of what it is doing: playing local files, acting as a USB sound card, receiving Bluetooth, or nothing. After changing an output route or source mode, the Player must be left with a valid one, or it stops playing.
_Avoid_: work mode, mode

**Gadget**:
The role the Disc's USB port is playing: nothing, a storage drive for a computer, a sound card for a computer, or a host driving an external DAC.
_Avoid_: port mode, network mode

### Switching

**Output sequence**:
The ordered set of commands that moves the Player to an output route, a source mode or a Bluetooth sink and leaves it with a valid Player work mode. Its order and timing matter; a sequence that fails part-way can leave the Player on a route the UI does not know about.
_Avoid_: switch sequence, init
