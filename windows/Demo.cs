// SPDX-License-Identifier: MIT
// Screenshot mode for the docs (DISKOS_DEMO=1): shows a PC where everything is in place, without running anything.
using System;

namespace DiskosDisco
{
    static class Demo
    {
        public static readonly bool On = Environment.GetEnvironmentVariable("DISKOS_DEMO") == "1";

        public static CheckResult For(Check c)
        {
            string d = c is WindowsCheck ? "Windows 11 (build 26100)."
                     : c is VirtualizationCheck ? "On."
                     : c is WslCheck ? "Installed (WSL version: 2.6.1.0)."
                     : c is UsbBridgeCheck ? "Installed (version 5.2.0)."
                     : c is SpaceCheck ? "212 GB free on C:."
                     : "Nothing waiting.";
            return new CheckResult(State.Ok, d);
        }
    }
}
