# Boomshine 1.2.0 — NeoN / Pop!

Standalone native chain-reaction game for the 360-pixel Snowsky Disc running
DiskOS Disco. This package does not replace mq_ui or change firmware.

## Play

Choose NeoN or Pop! on the landing page. Both use the same twenty-level game:
tap once to begin a chain reaction and catch at least the target number of dots.
The top label shows caught / target; the bottom shows the level number.
NeoN uses bright outlined bursts on black. Pop! uses soft pastel bursts on deep
blue. The edge colour is randomly selected from the mode's palette each level.
The playing field now extends to the edge, with five pixels reserved for the
border. Levels 1-12 retain their previous settings. Levels 13-20 add five dots each,
ending at 100 dots with a 96-dot target. Burst size reduces gradually in those
later levels so the higher density does not make them easier.

Hold a still finger on the screen for 0.85 seconds to return to the landing
page. A 3.5-second toast explains this whenever you choose a mode. The release
after a long hold is ignored, so it cannot accidentally start a new game.
There is no pause control. Exit on the landing page returns to DiskOS.
Swipes do not fire. A short tap fires on release, allowing long holds to be
recognized without spending the one shot.

Successful rounds add 10 points per caught dot plus 25 per level number.
Best score and unlocked level remain in scores.conf and survive an update.
Choosing either mode starts at level 1. The two modes share the existing score.
Music is not controlled by this app; the game is silent.

## Changes in 1.2.0

- Landing icon moved up 12 px; tagline 14 -> 18 px; Exit 18 -> 22 px and brighter.
- 20 levels, with the first twelve unchanged and eight denser, tighter rounds.
- Quarter-pixel alpha stamps for moving dots and full-size bursts. The cache is
  under 140 KiB and avoids repeating circle calculations at steady burst size.
- Collision radii calculated once per simulation step. Square roots occur only
  when a moving dot crosses the arena boundary.
- Fast framebuffer paths choose rotation and colour layout once per frame.
- Deadline-based 30 Hz display scheduling replaces fixed sleeps plus draw time.
  Slow frames skip deadlines instead of queuing a burst of delayed redraws.
- A touch begun on one screen cannot activate a control on the next screen.
- Lost touch packets resynchronize from the kernel, including a lost release;
  the interrupted gesture is consumed. A release-based fallback remains for
  drivers that lack state-query ioctls.
- Input processing is bounded per loop, preventing event floods from starving
  animation. Newly started levels do not inherit time from an idle menu.
- Corrected BusyBox installation command: decompress on the computer because
  this player's tar does not support -z.

## Diagnostics

The working 1.1.0 device discovery, rotation and launcher are preserved.
launch.sh records startup errors and display/touch details in
/tmp/boomshine.log. Retrieve this before rebooting or launching again if a
problem occurs. No fresh log means the launcher may not have run the script.

The user confirmed 1.1.0 works on the Disc. This 1.2.0 update has passed host
checks and a native MIPS build, but has not yet been tested on the device.

## Build

Use the DiskOS MIPS toolchain: GCC 11.2.0 / musl, MIPS32r2, FP64, NaN2008.

    make CROSS=/path/to/mipsel-n2008-musl-cross/bin/mipsel-linux-musl-
    make host
    make test
    make benchmark
    ./boomshine-host --render home home.ppm
    ./boomshine-host --render neon neon.ppm
    ./boomshine-host --render pop pop.ppm
    ./boomshine-host --render toast toast.ppm
    ./boomshine-host --render result result.ppm

fonts.h is included. Pillow is needed only to regenerate the embedded glyphs.
The preview scenes are deterministic staged frames from the actual renderer,
not images produced by a separate mockup system.

## Verification and limits

Warning-free host and MIPS builds. Static ELF / MIPS32r2 / FP64 / NaN2008 checked.
ASan/UBSan cover all 20 levels, boundaries, chain outcomes, single shot,
rotations, scaling, both modes, long holds, release suppression, swipes,
inter-screen touches, lost packets, legacy touch, and 100 simultaneous bursts.
Framebuffer output is checked against a reference mapper for four rotations,
two channel layouts, padded rows and a nonzero page offset, including guards.
First-twelve-level outcomes match 1.1.0 across 2,400 seeded simulation runs.

See BENCHMARKS.md for host-side comparisons. They measure CPU work on the same
computer, not device frame rate. Frame rate and music continuity for this
update have not been measured on hardware. The game still targets 30 fps with
60 Hz simulation and caps catch-up at 100 ms after a long stall.

The framebuffer drawing buffer occupies 518,400 bytes; reusable alpha masks
add about 139 kB. This is static storage, not measured process RAM use.
The renderer writes the visible framebuffer page; large updates can tear.
The display must expose 360x360, 32-bit pixels with 8-bit RGB channels.
