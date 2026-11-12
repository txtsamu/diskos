# USB Debug is an overlay on Local, not a Source mode

USB Debug is a row in the Working Mode picker, but selecting it leaves the Source mode at Local Playback and sends the Player no frame. The Player's mode thread builds and tears down USB gadgets whenever the `0642` selector changes, so a real Source mode would change the gadget under diskOS's own `diskos_dbg` gadget. The row is an overlay: diskOS owns the port, the session stays Local, and nothing is saved or restored at boot.

## Consequences

- The picker has a row that is not a Source mode. Entering it from another mode goes via Local first.
- Leaving stops the gadget before any Player switch, and the USB Audio route (host role) is refused while it is on.
- The root shell is off at every boot and never persisted, keeping "no always-on root shell" in `SECURITY.md`.
