// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - step 1: can this PC install diskOS? Each check returns a plain-language result
// and, when something is missing, the one action that fixes it.
using System;
using System.Diagnostics;
using System.IO;
using System.Management;
using System.Text;
using Microsoft.Win32;

namespace DiskosDisco
{
    enum State { Checking, Ok, Missing, Warning }

    class CheckResult
    {
        public State State;
        public string Detail;
        public string ActionText;   // null: nothing to click
        public Action Action;
        public CheckResult(State state, string detail) { State = state; Detail = detail; }
    }

    abstract class Check
    {
        public abstract string Title { get; }
        public abstract CheckResult Run();
    }

    static class Sys
    {
        // Run a program hidden and capture what it prints. wsl.exe prints UTF-16, everything else the console code page.
        public static int Run(string exe, string args, out string output, bool utf16 = false, int timeoutMs = 20000)
        {
            output = "";
            try
            {
                var psi = new ProcessStartInfo(exe, args)
                {
                    UseShellExecute = false, CreateNoWindow = true,
                    RedirectStandardOutput = true, RedirectStandardError = true,
                };
                if (utf16) { psi.StandardOutputEncoding = Encoding.Unicode; psi.StandardErrorEncoding = Encoding.Unicode; }
                using (var p = Process.Start(psi))
                {
                    var err = p.StandardError.ReadToEndAsync();
                    string o = p.StandardOutput.ReadToEnd();
                    if (!p.WaitForExit(timeoutMs)) { try { p.Kill(); } catch { } output = o; return -2; }
                    output = (o + err.Result).Replace("\0", "").Trim();
                    return p.ExitCode;
                }
            }
            catch (Exception e) { output = e.Message; return -1; }
        }

        public static string System32(string exe)
        {
            // A 32-bit process would be redirected to SysWOW64, where wsl.exe doesn't exist: use Sysnative then.
            string win = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string sysnative = Path.Combine(win, "Sysnative", exe);
            if (!Environment.Is64BitProcess && File.Exists(sysnative)) return sysnative;
            return Path.Combine(win, "System32", exe);
        }

        public static string FindOnPath(string exe)
        {
            foreach (string dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';'))
            {
                try { string f = Path.Combine(dir.Trim(), exe); if (dir.Length > 0 && File.Exists(f)) return f; } catch { }
            }
            return null;
        }

        public static void Open(string url)
        {
            try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
        }

        // Start something in a visible window, optionally asking Windows for administrator rights.
        public static void Launch(string exe, string args, bool admin)
        {
            try
            {
                var psi = new ProcessStartInfo(exe, args) { UseShellExecute = true };
                if (admin) psi.Verb = "runas";
                Process.Start(psi);
            }
            catch (System.ComponentModel.Win32Exception) { }   // the user said No to the administrator prompt
        }

        public static object Wmi(string cls, string prop)
        {
            try
            {
                using (var s = new ManagementObjectSearcher("SELECT " + prop + " FROM " + cls))
                    foreach (ManagementObject o in s.Get()) return o[prop];
            }
            catch { }
            return null;
        }
    }

    class WindowsCheck : Check
    {
        public override string Title { get { return "Windows 11, 64-bit"; } }
        public override CheckResult Run()
        {
            int build = 0;
            using (var k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                if (k != null) int.TryParse((k.GetValue("CurrentBuildNumber") ?? "0").ToString(), out build);
            string arch = Environment.GetEnvironmentVariable("PROCESSOR_ARCHITEW6432")
                          ?? Environment.GetEnvironmentVariable("PROCESSOR_ARCHITECTURE") ?? "";
            if (arch.ToUpperInvariant() == "ARM64")
                return new CheckResult(State.Missing, "This PC has an ARM processor. diskOS needs a regular (Intel or AMD) 64-bit PC for now.");
            if (!Environment.Is64BitOperatingSystem)
                return new CheckResult(State.Missing, "This is 32-bit Windows. diskOS needs 64-bit Windows 11.");
            if (build >= 22000) return new CheckResult(State.Ok, "Windows 11 (build " + build + ").");
            if (build >= 19041)
                return new CheckResult(State.Warning, "Windows 10 (build " + build + "). It may work, but only Windows 11 is tested.");
            return new CheckResult(State.Missing, "This Windows version (build " + build + ") is too old. Update to Windows 11.");
        }
    }

    class VirtualizationCheck : Check
    {
        public override string Title { get { return "Virtualization turned on"; } }
        public override CheckResult Run()
        {
            object hv = Sys.Wmi("Win32_ComputerSystem", "HypervisorPresent");
            if (hv is bool && (bool)hv) return new CheckResult(State.Ok, "On.");
            object fw = Sys.Wmi("Win32_Processor", "VirtualizationFirmwareEnabled");
            if (fw is bool && (bool)fw) return new CheckResult(State.Ok, "On (Windows starts using it once WSL is installed).");
            var r = new CheckResult(State.Missing,
                "Virtualization is off in this PC's firmware (BIOS/UEFI) settings. WSL needs it. It's usually called " +
                "Intel VT-x, Intel Virtualization Technology, AMD-V or SVM. Turn it on, save and restart.");
            r.ActionText = "How to turn it on";
            r.Action = () => Sys.Open("https://support.microsoft.com/windows/enable-virtualization-on-windows-c5578302-6e43-4b4b-a449-8ced115f58e1");
            return r;
        }
    }

    class WslCheck : Check
    {
        public override string Title { get { return "WSL (Windows Subsystem for Linux)"; } }
        public override CheckResult Run()
        {
            string wsl = Sys.System32("wsl.exe");
            string output = "";
            int rc = File.Exists(wsl) ? Sys.Run(wsl, "--version", out output, utf16: true) : -1;
            if (rc == 0)
            {
                string first = output.Split('\n')[0].Trim();
                return new CheckResult(State.Ok, "Installed" + (first.Length > 0 ? " (" + first + ")." : "."));
            }
            var r = new CheckResult(State.Missing,
                "diskOS builds and flashes the Disc inside WSL, Microsoft's free Linux layer for Windows. Click Install " +
                "WSL and say Yes to the administrator question. When it finishes, restart Windows and open this app again.");
            r.ActionText = "Install WSL";
            r.Action = () => Sys.Launch(wsl, "--install --no-distribution", admin: true);
            return r;
        }
    }

    class UsbBridgeCheck : Check
    {
        public override string Title { get { return "USB bridge (usbipd-win)"; } }
        public static string Find()
        {
            string f = Sys.FindOnPath("usbipd.exe");
            if (f != null) return f;
            string pf = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            f = Path.Combine(pf, "usbipd-win", "usbipd.exe");
            return File.Exists(f) ? f : null;
        }
        public override CheckResult Run()
        {
            string exe = Find();
            string output;
            if (exe != null && Sys.Run(exe, "--version", out output) == 0)
                return new CheckResult(State.Ok, "Installed (version " + output.Split('\n')[0].Trim() + ").");
            var r = new CheckResult(State.Missing,
                "This free tool passes the Disc's USB connection through to WSL. Click Install, follow the installer " +
                "(Next, Install, Finish), then click Check again.");
            string winget = Sys.FindOnPath("winget.exe");
            r.ActionText = "Install USB bridge";
            if (winget != null)
                r.Action = () => Sys.Launch(winget,
                    "install --exact --id dorssel.usbipd-win --interactive --accept-package-agreements --accept-source-agreements", admin: false);
            else
                r.Action = () => Sys.Open("https://github.com/dorssel/usbipd-win/releases/latest");
            return r;
        }
    }

    class SpaceCheck : Check
    {
        const long NeedGB = 10;
        public override string Title { get { return "Free disk space"; } }
        public override CheckResult Run()
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var drive = new DriveInfo(Path.GetPathRoot(local));
            long gb = drive.AvailableFreeSpace / (1024L * 1024 * 1024);
            if (gb >= NeedGB) return new CheckResult(State.Ok, gb + " GB free on " + drive.Name.TrimEnd('\\') + ".");
            return new CheckResult(State.Missing,
                "Only " + gb + " GB free on " + drive.Name.TrimEnd('\\') + ". diskOS needs about " + NeedGB +
                " GB while it prepares the firmware. Free up some space, then click Check again.");
        }
    }

    class RestartCheck : Check
    {
        public override string Title { get { return "No restart waiting"; } }
        public override CheckResult Run()
        {
            bool pending = KeyExists(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending")
                        || KeyExists(@"SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired");
            if (!pending) return new CheckResult(State.Ok, "Nothing waiting.");
            var r = new CheckResult(State.Missing,
                "Windows needs a restart to finish installing something (often WSL). Restart, then open this app again.");
            r.ActionText = "Restart now";
            r.Action = () => Sys.Launch(Sys.System32("shutdown.exe"), "/r /t 5 /c \"Restarting to finish setting up for diskOS\"", admin: false);
            return r;
        }
        static bool KeyExists(string path)
        {
            using (var k = Registry.LocalMachine.OpenSubKey(path)) return k != null;
        }
    }
}
