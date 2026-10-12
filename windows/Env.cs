// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - step 2: diskOS's own private Linux environment (a WSL distribution named
// "diskOS-Disco"). It is imported from diskos-wsl.tar.gz (built by wsl/build-env.sh) and never touches other
// WSL distributions on the PC.
using System;
using System.IO;
using System.Linq;

namespace DiskosDisco
{
    static class Env
    {
        public const string Name = "diskOS-Disco";
        public const string Package = "diskos-wsl.tar.gz";

        static string Wsl { get { return Sys.System32("wsl.exe"); } }

        public static string Home
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "diskOS-Disco"); }
        }

        public static bool Exists()
        {
            string o;
            if (Sys.Run(Wsl, "--list --quiet", out o) != 0) return false;
            return o.Split('\n').Any(l => l.Trim().Equals(Name, StringComparison.OrdinalIgnoreCase));
        }

        // The package next to the app, or in the app's own folder.
        public static string FindPackage()
        {
            foreach (string dir in new[] { AppDomain.CurrentDomain.BaseDirectory, Home })
            {
                string f = Path.Combine(dir, Package);
                if (File.Exists(f)) return f;
            }
            return null;
        }

        public static int Import(string package, out string output)
        {
            string dir = Path.Combine(Home, "wsl");
            Directory.CreateDirectory(dir);
            return Sys.Run(Wsl, "--import " + Name + " \"" + dir + "\" \"" + package + "\" --version 2", out output, timeoutMs: 15 * 60 * 1000);
        }

        public static int Remove(out string output)
        {
            return Sys.Run(Wsl, "--unregister " + Name, out output, timeoutMs: 5 * 60 * 1000);
        }

        // Run a command inside the environment (as root, in /root).
        public static int Run(string command, out string output, int timeoutMs = 5 * 60 * 1000)
        {
            return Sys.Run(Wsl, "-d " + Name + " --cd /root -- " + command, out output, timeoutMs: timeoutMs);
        }

        public static string Version()
        {
            string o;
            return Run("cat /opt/diskos/WSL_ENV_VERSION", out o, 60000) == 0 ? o.Trim() : null;
        }
    }
}
