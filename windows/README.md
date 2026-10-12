# diskOS Disco! for Windows (in progress, planned for 2.0)

A Windows app that installs diskOS Disco! without typing commands. Plan: `WINDOWS_PLAN` (project doc) and
`../todo-ideas.md`. Underneath it uses WSL and usbipd-win, the route proven by a full flash on Windows 11.

Status: **steps 1-2**: the PC check (Windows 11 x64, virtualization, WSL, USB bridge, disk space, pending restart)
with a button for whatever is missing, and diskOS's own WSL environment ("diskOS-Disco", imported from
`diskos-wsl.tar.gz`, runs as root so no udev rule is needed). Next: the install wizard, then SD updates.

`sudo sh wsl/build-env.sh` builds the environment package (minimal Ubuntu 24.04, installer, Python deps, static
usbboot, current payload) into `out/`.

Build on Linux with `sh build.sh` (needs `mono-devel`). The result is one small EXE that runs on the .NET Framework
already in Windows. Test builds aren't code-signed, so Windows shows "Windows protected your PC": click
**More info**, then **Run anyway**.
