# diskOS player UI

The on-device UI that replaces the stock interface on the Disc and drives the stock Player over its command channel. This glossary names what the UI decides about audio: where sound comes from, where it goes, and the steps between.

## Language

**Disc**:
The handheld music player hardware that runs diskOS.
_Avoid_: device, unit

**Player**:
The stock playback firmware process on the Disc that the UI sends commands to. It owns decoding, output and the USB port; the UI only asks.
_Avoid_: engine, daemon, mq_player (a binary name, not the concept)

**Player config**:
The Player's own configuration database (`sysconfig.db`, table SYSCONFIG, one row). The Player owns it and loads it at start: the UI reads it to seed its own settings, changes only AUTO_TIME in it directly, and resets the work mode column to Local at boot and before handing off to the Player. All access goes through one module (`sysconfig`).
_Avoid_: sysconfig (a file and table name, not the concept), settings DB, stock config

**SD access**:
Whether diskOS may read or write the SD card at all. One authority (`sdio`) combines the launcher's launch verdict, the boot-time hold, the storage phase (Local, draining, waiting for a computer, USB Storage, waiting to return, Unknown), the decoder-fault flag and quiesce into a single admission: readers (including art decoder children) and writers are counted together and the card is "drained" only when both reach zero. Fail-closed: Unknown or held stays closed until the next boot. The USB Storage handoff marker file makes the phase survive a UI restart.
_Avoid_: SD lock, storage mode (that is the Source mode)

**Cover art**:
The cover image for the current track plus the accent colour derived from it, kept warm in the art cache and in the music database so Now Playing, the screensaver, the album wall and the library can show them without waiting for a decode. One module (`coverart`) owns requesting, caching, the online fallback, background prewarming and accent extraction.
_Avoid_: artwork pipeline, album art (the album wall is one consumer)

### Audiobooks and playing

**Play request**:
The lifecycle of one play command sent to the Player (arm, started, failed, timed out), owned by `playreq`. A Play request is *pending* from the moment its list play is sent until the Player shows track and stream evidence, the Player or transport epoch changes, or it times out (15 s unobserved, 6 s without progress, 45 s hard limit). A play is *recent* for 10 s of its Player generation after a play-list or queue-jump send, or while a Play request is pending. This is distinct from the transport latch, which also records plain transport commands and observed playback.

**Book session**:
A deliberate audiobook play the UI is tracking: it owns the bookmark for that book, applies the owed resume seek once, backs up by a smart rewind after a pause, and checkpoints the position about every 10 s. It ends on any explicit track change, a backward jump of more than 35 s that is not a user seek, or the Player restarting. A book the Player is already playing when the UI starts is *adopted* into a session.
_Avoid_: book mode, resume (that is one step of the session)

**Sleep timer**:
A duration timer or an end-of-chapter (EOC) target, owned by `booksess`, which pauses (or shuts down) when reached and is held off for 2.5 s after a user seek or transport tap.

### Modes and routes

**Source mode**:
What the Disc is currently for: Local playback, USB DAC, Bluetooth Receiving or USB Storage. Exactly one is active.
_Avoid_: input, audio source

**Working Mode**:
The user-facing picker through which a source mode is chosen, and the choice itself that survives a restart.
_Avoid_: mode (alone, ambiguous with Player work mode and play mode)

**Output route**:
Where Local playback's sound goes: the internal DAC, SPDIF, USB Audio, or a Bluetooth sink. Only meaningful while the source mode is Local. One module (`outroute`) owns it, the Bluetooth sink it points at, and the steps for moving it. Moving it pauses the Player first and, depending on who asked, either restores the internal DAC and resumes if the move fails (the output picker) or leaves the Player paused (a Bluetooth or analog route request).
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

**Route policy**:
What happens to playback when an Output sequence for a route fails part-way. "Recover" restores the internal DAC and resumes; "leave paused" reports the failure and leaves the Player paused. The two differ today by history, not by decision.
_Avoid_: failure mode
