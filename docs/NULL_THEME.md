# Null 2.3.0-Null.1 — Disc device-test build

Select Display > Theme > Null. Theme settings are under Display > Null Options.

Includes the pixel typography and transports, cyan/yellow and red/lime palettes,
sliding navigation, vertical 4-cover-4 quick settings, background choices,
text alignment, and buttons/autofade/gesture controls. Custom app interiors are retained.

Built with GCC 11.2.0 for static MIPS32r2, FP64, NaN2008.
Key host interaction checks and the transport checks passed. Physical Disc
validation remains pending. This is a development candidate, not a stable release.

Use the existing signed SD installation workflow and recovery/rollback procedure.
The raw mq_ui is the UI payload, not a signed SD update package.

Source is supplied in Null-source.tar.gz. Build with ui/tools/build_device.py
and the toolchain-gcc11.2 release toolchain documented in ui/README.md.
The build manifest records the source digest and exact binary checksums.
