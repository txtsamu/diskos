# Reach non-Local Working Modes through Local Playback

A switch between two non-Local Working Modes goes **via Local**: the UI returns the Player to Local Playback, waits for it to settle, then enters the target. Direct `0642` transitions are kept only where the Player's own mode thread handles them safely. Several direct paths did not: switching from the host-role USB Audio route straight to USB Storage never reached the computer and held SD access until a restart, the Player's storage selector is not fully decompiled, and the Player applies a selector only when its value changes.

## Considered Options

- **Direct `0642` frames for every transition.** Rejected: the unsafe paths above have no recovery, and a failed direct switch can leave the Player on a route the UI does not know about.
- **Go via Local as a deferred step inside `srcswitch`.** Chosen. USB Storage is the exception that proves the rule: returning the card restarts the UI, so the target is saved as a one-shot pending switch and entered once the Player has settled.

## Consequences

- A switch shows "Leaving ..." and takes seconds longer. Leaving a USB DAC with a computer attached runs the ordered exit first (14-17 s).
- A failed via Local switch ends in Local Playback, never in an unknown Player work mode.
- A new mode must decide whether it can enter direct or needs via Local, and `source_switch` refuses new requests while one is waiting.
