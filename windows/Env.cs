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
        public const string Generation = "diskos-disco-wsl 3";   // must match wsl/build-env.sh; older ones get set up again

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

        // The package next to the app, or its parts (.001, .002, ... plus .sha256) joined into the app's folder.
        // Parts next to the app win over a copy joined earlier, unless that copy is the same package.
        public static string FindPackage()
        {
            string here = AppDomain.CurrentDomain.BaseDirectory;
            string whole = Path.Combine(here, Package);
            if (File.Exists(whole)) return whole;
            Directory.CreateDirectory(Home);
            string joined = Path.Combine(Home, Package);
            string sumFile = Path.Combine(here, Package + ".sha256");
            if (!File.Exists(Path.Combine(here, Package + ".001")) || !File.Exists(sumFile))
                return File.Exists(joined) ? joined : null;
            string want = File.ReadAllText(sumFile).Trim().Split(' ')[0].ToLowerInvariant();
            if (File.Exists(joined) && Sha256(joined) == want) return joined;
            string part = joined + ".part";
            using (var outp = File.Create(part))
                for (int i = 1; ; i++)
                {
                    string p = Path.Combine(here, Package + "." + i.ToString("000"));
                    if (!File.Exists(p)) break;
                    using (var inp = File.OpenRead(p)) inp.CopyTo(outp);
                }
            if (Sha256(part) != want) { File.Delete(part); return null; }   // a part is missing or damaged
            if (File.Exists(joined)) File.Delete(joined);
            File.Move(part, joined);
            return joined;
        }

        static string Sha256(string file)
        {
            using (var sha = System.Security.Cryptography.SHA256.Create())
            using (var f = File.OpenRead(file))
                return BitConverter.ToString(sha.ComputeHash(f)).Replace("-", "").ToLowerInvariant();
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

        // C:\Users\me\x.zip -> /mnt/c/Users/me/x.zip (null for network paths, which WSL can't reach this way)
        public static string ToLinux(string win)
        {
            win = Path.GetFullPath(win);
            if (win.Length < 3 || win[1] != ':' || !char.IsLetter(win[0])) return null;
            return "/mnt/" + char.ToLowerInvariant(win[0]) + win.Substring(2).Replace('\\', '/');
        }

        // Quote for the Linux shell: 'it'\''s'
        public static string Q(string s) { return "'" + s.Replace("'", "'\\''") + "'"; }

        // Run a shell script inside the environment, line by line. The script is written to the app's folder and
        // started from there, so no quoting passes through the Windows command line.
        public static int Script(string body, Action<string> onLine)
        {
            Directory.CreateDirectory(Home);
            File.WriteAllText(Path.Combine(Home, "run.sh"), "set -eu\n" + body.Replace("\r\n", "\n") + "\n", new System.Text.UTF8Encoding(false));
            return Sys.RunLines(Wsl, "-d " + Name + " --cd \"" + Home + "\" -- sh ./run.sh", onLine);
        }

        public static int Script(string body, out string output)
        {
            var sb = new System.Text.StringBuilder();
            int rc = Script(body, l => { lock (sb) sb.AppendLine(l); });
            output = sb.ToString().Trim();
            return rc;
        }

        // A hidden "sleep" inside the environment keeps it running while the USB bridge attaches the Disc to it.
        // (The bridge loads the vhci_hcd kernel module through this environment, hence kmod in the package.)
        public static System.Diagnostics.Process KeepAlive()
        {
            return Sys.Background(Wsl, "-d " + Name + " -- sleep 86400");
        }

        // The installer's state (including the backup of the Disc's original system) lives inside the environment.
        // A copy is kept in a normal Windows folder, so removing or replacing the environment never loses it.
        public const string StateDir = "/root/.local/share/diskos-installer";
        public static string BackupDir { get { return Path.Combine(Home, "backup"); } }

        public static int SaveBackup(out string output)
        {
            string win = ToLinux(BackupDir);
            return Script("S=" + StateDir + "\n[ -d \"$S/stock\" ] || { echo 'nothing to back up yet'; exit 0; }\n" +
                          "mkdir -p " + Q(win) + "\nrm -rf " + Q(win + "/stock.new") + "\n" +
                          "cp -r \"$S/stock\" " + Q(win + "/stock.new") + "\n[ -f \"$S/state.json\" ] && cp \"$S/state.json\" " + Q(win + "/state.json") + "\n" +
                          "rm -rf " + Q(win + "/stock") + "\nmv " + Q(win + "/stock.new") + " " + Q(win + "/stock") + "\necho 'backup copied to Windows'", out output);
        }

        public static int RestoreBackup(out string output)
        {
            output = "";
            if (!Directory.Exists(Path.Combine(BackupDir, "stock"))) return 0;
            string win = ToLinux(BackupDir);
            return Script("S=" + StateDir + "\n[ -d \"$S/stock\" ] && { echo 'environment already has a backup'; exit 0; }\n" +
                          "mkdir -p \"$S\"\ncp -r " + Q(win + "/stock") + " \"$S/stock\"\n" +
                          "[ -f " + Q(win + "/state.json") + " ] && cp " + Q(win + "/state.json") + " \"$S/state.json\"\n" +
                          "echo 'backup of the original Disc system restored into the environment'", out output);
        }

        public static string Version()
        {
            string o;
            return Run("cat /opt/diskos/WSL_ENV_VERSION", out o, 60000) == 0 ? o.Trim() : null;
        }
    }
}
