# USB Debug (Source menu)

**USB Debug** is a row in the Working Mode picker (Quick Settings > source, or Settings > Audio > Working Mode). It puts the
USB-C port into a diskOS-owned gadget with one CDC-ACM serial port. On the Disc the shell runs on `/dev/ttyGS0`; on the computer
it shows up as `/dev/ttyACM0` (Linux), `/dev/cu.usbmodem*` (macOS) or a COM port (Windows). Leaving the row (back to Local
Playback, or to USB DAC, Bluetooth Receiving, USB Storage) removes the gadget first and then does the normal switch.

Status: built, host-tested, and run on a real Disc with a Linux PC attached (see *What was verified*). Not flashed, not
cold-booted, and USB DAC / USB Storage were not exercised after USB Debug on hardware.

## What it does not do: USB Ethernet (ECM / RNDIS)

The request was one gadget with a serial shell **and** a USB Ethernet link (ECM, RNDIS only if ECM were impossible) so `ssh`
would work over the cable. That is **not possible on this kernel**, so it is not faked. Evidence, from the live Disc (Linux
4.4.94 `#236`, V2.57 firmware):

- The kernel ships no `/proc/config.gz` and no `/lib/modules`; the only modules are Wi-Fi/SoC drivers in `/module_driver`.
- `/proc/kallsyms` has no `ecm_*`, `rndis_*`, `ncm_*`, `eem_*`, `gether_*` or `u_ether*` symbols. The registered gadget
  function table (`*usb_func` symbols) is exactly ten entries: `acm gser obex mass_storage ffs uac1 uac2 hid mtp ptp`.
- configfs says the same thing. On a scratch gadget (`mount -t configfs none /sys/kernel/config`, `mkdir
  usb_gadget/zz_probe`, then `mkdir functions/<name>`): `acm.0 gser.0 mass_storage.0 uac2.a hid.0 ffs.x obex.0 mtp.0` work;
  `ecm.0 rndis.0 ncm.0 eem.0 geth.0 subset.0 midi.0 loopback.0 SourceSink.0 uvc.0 printer.0` all fail with `No such file or
  directory`. There is no `modprobe` path to load them.
- The fallbacks are closed too: no `tun` (`tun_chr_open` absent, no `/dev/net`), no PPP (`ppp_register_channel`,
  `ppp_input` absent), no SLIP, and no `pppd` or `slattach` in userland. FunctionFS (`ffs`) is present, but a userspace
  Ethernet function on top of it would still need a TUN/TAP device, which the kernel lacks.

What would be needed: a kernel built with `CONFIG_USB_CONFIGFS_ECM` (and `u_ether`) or `CONFIG_USB_CONFIGFS_RNDIS`, plus
`CONFIG_TUN` if a userspace route is wanted, or matching out-of-tree modules built against the vendor 4.4.94 build tree. That
means a kernel flash. The kernel is the stock FiiO one (the installer re-uses it), and its source/build tree is not in this
repository, so this was not attempted. Until then, `ssh` still works over Wi-Fi: key-only on port 2222 (`key-on`, Debug Mode)
and the temporary password SSH on port 22 (`ssh-on`). Neither is changed by USB Debug, and nothing new listens on a USB
interface (there is none).

## Who owns the USB port in each mode

The Player (`mq_player`) owns the UDC for every stock mode. Its mode thread applies the `0642` selector only when the
selector value *changes* (`FUN_004e5af4`, `loop_mode_handle_thread`): on a change it stops the old gadget (`uac_demo` or
`storage_demo`, then `rmdir` and `umount /sys/kernel/config`), writes `device`/`host` to
`/sys/class/usb_role/13500000.otg_new-role-switch/role`, and builds the new one with `mount -t configfs none
/sys/kernel/config`, `mkdir usb_gadget/<name>` and a write of `13500000.otg_new` to `UDC`. Selector `0` (Local) builds
nothing; with the selector already at `0`, sending `0642 0` again is a no-op.

| Mode | Owner of the UDC | Gadget (configfs name) | Role file |
|---|---|---|---|
| Local Playback | nobody (UDC unbound, `not attached`) | none | `none` after boot, `device` after a stock mode returned |
| USB DAC | Player | `uac_demo` (`uac2.a`) | `device` |
| USB Storage | Player | `storage_demo` (`mass_storage.0`) | `device` |
| USB Audio (external DAC) | Player | none (host role, VBUS on) | `host` |
| **USB Debug** | **diskOS** (`diskos-debug.sh usb-on`) | **`diskos_dbg`** (`acm.0`, VID 0525 PID a4a7, class 02) | `device` |

USB Debug is an **overlay on Local**, not a Source mode: the session still says Local (`session_source_mode() == 0`), the
UI sends **no Player frames** to enter or leave it, and playback keeps working. It is not saved (`src_mode` is untouched)
and **it is never restored at boot**: after a reboot the Disc is in plain Local Playback with the port idle.

## Switch sequence

Enter (`srcswitch.c` `dbg_enter`, `usbdbg.c`):

1. Refuse when a stock gadget exists (`uac_demo`, `storage_demo`, `adb_demo`): "Leave USB DAC or USB Storage first".
2. If the Source mode is not Local, the USB Audio route is on (host role) or the card is exported: go via Local first
   (`via_local`, the same ordered path every other mode uses, including the safe USB DAC exit with a host attached).
3. Run `/usr/project/diskos-debug.sh usb-on` detached (own process group, never blocks the UI thread; 45 s limit then
   abort and clean-up). The script: refuse if a stock gadget is present; mount configfs only if missing (remembered in
   `/tmp/.diskos_usbdbg/mounted`); remove a legacy `serial_demo`; build `diskos_dbg`; wait up to 10 s for the UDC; write
   `device` to the role file (skipped when it already says `device`) and wait 1 s like the Player does; write the UDC name to `UDC`; wait up to
   8 s for `/dev/ttyGS0`; start the single shell supervisor on it.
4. The UI polls the gadget directory and the UDC attribute (not the script's word) and shows ON.

Leave (`dbg_leave_begin`, `dbg_exit_tick`):

1. `diskos-debug.sh usb-off` runs detached: stop the shell, write an empty string to `UDC`, wait (up to 10 s) until it reads
   empty, remove the gadget directories, unmount configfs only if the script mounted it. The role file is left at `device`:
   that is where the Player leaves it after every stock mode, and writing it back would race a Player that is already
   switching modes.
2. When the gadget is gone, wait 1 s (the same margin the USB DAC exit uses) and only then continue to the target mode. For Local
   nothing is sent. For USB DAC, Bluetooth Receiving or USB Storage the normal `source_switch` runs, so the Player's
   `0642` change always happens with the UDC free.

Failure and rollback:

- `usb-on` fails (no UDC, bind error, tty missing, timeout): the script cleans up after itself, the UI cleans up again with
  `usb-off` if the gadget directory is still there, toasts the reason, and ends in Local with nothing bound.
- `usb-off` cannot unbind within its time: the UI stays ON, toasts "Couldn't stop USB Debug", abandons the pending switch
  and sends nothing to the Player, so the Player never changes the selector while our gadget is bound.
- An old `diskos-debug.sh` (no `usb-on`): the script prints its usage and exits 1; the UI says "USB Debug needs a newer diskOS
  image". The script ships in the image, so a hot-loaded `mq_ui` alone is not enough: flash, or bind-mount the new script
  over `/usr/project/diskos-debug.sh` for a test.
- The USB Audio row (host role) is refused while USB Debug is on ("Leave USB Debug first"), also for the boot-time
  USB Audio restore: flipping to host with our gadget bound is the one transition the Player would not undo.
- A UI restart or crash keeps the gadget: the new `mq_ui` adopts a bound `diskos_dbg` at start and keeps polling, so a gadget
  started or stopped from SSH shows up in the row within a second.

### `usb-on` exit codes

`ui/usbdbg.c` reads the script's exit status and turns it into the toast. The numbers are a
private contract between `payload/diskos-debug.sh` and `ui/usbdbg.c`: changing one side means
changing the other. These are runtime script codes, not the installer's `E`/`F` codes.

| Exit | Meaning | Toast |
|---:|---|---|
| 0 | Gadget built or already bound; shell started | none |
| 1 | Usage (no command, or an old script) | USB Debug needs a newer diskOS image |
| 2 | A stock gadget is present (`uac_demo`, `storage_demo`, `adb_demo`) | Leave USB DAC or USB Storage first |
| 3 | No USB device controller appeared within the wait | No USB controller found |
| 4 | The UDC write did not bind the gadget | Couldn't bind the USB port |
| 5 | Setup failed: configfs, legacy serial removal, or gadget build | Couldn't set up USB Debug |
| 6 | The serial port did not appear | USB serial port did not appear |
| 127 | The shell could not find `usb-on` | USB Debug needs a newer diskOS image |

`usb-off` returns 0 on success and 1 when it cannot unbind or remove the gadget; the UI toasts
"Couldn't stop USB Debug", stays ON and abandons the pending switch.

## The dev-image boot hook (`S99usbserial`)

Dev images (`--variant dev`) used to run a loop that re-bound `serial_demo` to the UDC every 2 s and kept ttyGS0 shells
alive. Nothing else could ever bind the UDC while it ran, so on a dev image the stock USB Storage and USB DAC gadgets could not
bind (`UDC` write `EBUSY`), and the loop would have fought any switch. It is now a one-shot: it runs `usb-on` up to three times
(5 s apart; no retry when a stock gadget is found) and exits. After that the UI owns the port like on a public image, and a dev
image simply starts with USB Debug already on (the UI adopts it); Local Playback in the picker turns it off. Images flashed before
this change keep the old loop until reflashed; `usb-on` removes a leftover `serial_demo`, after which the old loop is harmless.

## Security

- The shell on `ttyGS0` is a **root shell with no password**, as the dev build's serial shell always was. Anyone who can plug
  into the Disc's USB port while USB Debug is on has root. The row says so on screen (`Root shell, no password`).
- Public images had no USB serial shell at all (`SECURITY.md`). USB Debug adds one **on request**: it is off at every boot, it
  is never persisted, the Persistent debug checkbox does not touch it, and it ends when you leave the row. That keeps "no
  always-on root shell"; it does change "public builds have no serial shell", so `SECURITY.md` and `INSTALL.md` were updated.
  If you would rather keep it out of public images, gate the row on `VARIANT=dev` in `/etc/diskos_manifest`.
- Nothing new listens on the network. Key-only SSH (port 2222) and the temporary password SSH (port 22) are unchanged.

## Using it

On the Disc: Quick Settings > source > **USB Debug**. The row turns to "Plug USB-C into a PC", then "Open /dev/ttyACM0 on the PC"
once the computer has configured the port, and a panel under the row shows the command.

On the computer:

```sh
ls /dev/ttyACM*                       # appears a second or two after the row says "Open ..."
picocom -b 115200 /dev/ttyACM0        # or: screen /dev/ttyACM0 115200 ; tio /dev/ttyACM0
```

(no `picocom`? `python3 -c "import serial"` is not needed: `stty -F /dev/ttyACM0 raw -echo; cat /dev/ttyACM0 &` and `echo ls >
/dev/ttyACM0` also work). Linux: your user needs the `dialout` group. Windows 10+ uses its inbox CDC driver: pick the new COM
port in PuTTY (Serial, 115200). The baud rate is ignored by a CDC link. The gadget is VID:PID `0525:a4a7`, product "diskOS USB Debug".

`lsusb | grep 0525:a4a7` should list it; if the computer shows nothing, check the cable carries data and `ssh disc
'sh /usr/project/diskos-debug.sh usb-status'` (`USBDBG=on ... HOST=configured`).

## What was verified (2026-11-10)

On a Snowsky Disc (V2.57, Linux 4.4.94, public image) with its USB port cabled to a Fedora PC:

- `diskos-debug.sh usb-on` / `usb-off` / `usb-status` run directly and from the new `mq_ui`: gadget built, UDC bound, `ttyGS0`
  created, PC enumerates `0525:a4a7` and creates `/dev/ttyACM0`, a root shell answers over it, `usb-off` leaves the port idle
  (configfs unmounted, no gadget directory, no `ttyGS0`, no stray processes, UDC `not attached`, role `device`).
- From the picker with real taps: USB Debug row selected with a check mark, sub-label and PC command panel; the USB Audio row
  refused with the toast while it is on; Local Playback runs the ordered exit and clears the mark. The host tests assert that
  no Player frame is sent; on the Disc `fiio_player.log` showed only the USB state uevents (connected / disconnected) around the
  gadget going up and down, no output or gadget re-initialisation from the mode change itself.
- UI relaunch with a gadget bound adopts it; `usb-on` / `usb-off` from SSH are noticed by the running UI.

Not verified on hardware: a flashed image (the script was bind-mounted for the test), a cold boot, a dev image boot hook,
USB DAC / USB Storage / USB Audio entered *after* USB Debug (covered by host tests only), playback while toggling, macOS and
Windows hosts, a charger-only cable.
