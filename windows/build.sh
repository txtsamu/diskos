#!/bin/sh
# Build the Windows app from Linux (needs mono-devel). Output: windows/out/diskOS-Disco.exe
# It is a .NET Framework 4.x app: Windows 10/11 already include the runtime, so it is one small EXE.
set -eu
cd "$(dirname "$0")"
mkdir -p out
mcs -target:winexe -platform:x64 -optimize+ -codepage:utf8 -sdk:4.5 \
    -r:System.Windows.Forms.dll -r:System.Drawing.dll -r:System.Management.dll \
    -out:out/diskOS-Disco.exe *.cs
sha256sum out/diskOS-Disco.exe
