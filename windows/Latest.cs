// SPDX-License-Identifier: GPL-3.0-or-later
// The latest diskOS Disco! interface (mq_ui) from GitHub, checked against the release's SHA256SUMS.
// Used by Install (first install) and Update (SD card): both use the newest release unless the user picks a file.
using System;
using System.IO;
using System.Linq;
using System.Net;

namespace DiskosDisco
{
    static class Latest
    {
        public const string Repo = "https://github.com/zmd22/diskos-disco/releases";

        // Downloads mq_ui of the latest release into dir. True with file + tag ("v2.5.6"), else false with a reason.
        public static bool Fetch(string dir, Action<string> log, out string file, out string tag, out string error)
        {
            file = null; error = null;
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
            tag = Tag();
            if (tag == null) { error = "Couldn't reach GitHub. Check the internet connection and try again."; return false; }
            if (log != null) log("Latest release: " + tag);
            try
            {
                Directory.CreateDirectory(dir);
                string f = Path.Combine(dir, "mq_ui"), sums = Path.Combine(dir, "SHA256SUMS");
                using (var w = new WebClient())
                {
                    w.Headers[HttpRequestHeader.UserAgent] = "diskOS-Disco-for-Windows";
                    w.DownloadFile(Repo + "/download/" + tag + "/mq_ui", f);
                    w.DownloadFile(Repo + "/download/" + tag + "/SHA256SUMS", sums);
                }
                string want = File.ReadAllLines(sums).Select(l => l.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries))
                                  .Where(p => p.Length == 2 && p[1].TrimStart('*') == "mq_ui").Select(p => p[0].ToLowerInvariant()).FirstOrDefault();
                string got;
                using (var sha = System.Security.Cryptography.SHA256.Create()) using (var s = File.OpenRead(f))
                    got = BitConverter.ToString(sha.ComputeHash(s)).Replace("-", "").ToLowerInvariant();
                if (want == null || want != got) { error = "The download doesn't match the release's checksum. Try again."; return false; }
                if (log != null) log("Downloaded mq_ui " + tag + ", checksum OK (" + got.Substring(0, 12) + "…).");
                file = f;
                return true;
            }
            catch (Exception e) { error = "The download failed: " + e.Message; return false; }
        }

        // Brings the environment's installer files up to the latest release, if they changed since the last run:
        // the installer, its Python package, payload/ (mq_ui and the boot scripts), licences and the key/sign scripts,
        // from the release's source archive. The archive's mq_ui must match the release's SHA256SUMS, else nothing is
        // replaced. The swap keeps the old files until the new ones are in place (and restores them if the Python
        // requirements can't be installed). Offline, or on any failure, the environment stays exactly as it was.
        // Returns a line for the log; null when nothing needed doing.
        public static string SyncEnvironment(Action<string> log)
        {
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
            string tag = Tag();
            if (tag == null) return "Couldn't check GitHub for a newer release; using the installer files already here.";
            string have; Env.Run("cat /opt/diskos/RELEASE_TAG 2>/dev/null", out have, 60000);
            if ((have ?? "").Trim() == tag) return null;                                   // already the latest
            if (log != null) log("Updating the installer files to " + tag + "…");
            try
            {
                Directory.CreateDirectory(Env.Home);
                string src = Path.Combine(Env.Home, "release-src.tar.gz"), sums = Path.Combine(Env.Home, "release-SHA256SUMS");
                using (var w = new WebClient())
                {
                    w.Headers[HttpRequestHeader.UserAgent] = "diskOS-Disco-for-Windows";
                    w.DownloadFile("https://github.com/zmd22/diskos-disco/archive/refs/tags/" + tag + ".tar.gz", src);
                    w.DownloadFile(Repo + "/download/" + tag + "/SHA256SUMS", sums);
                }
                string o;
                int rc = Env.Script(
                    "S=$(mktemp -d); trap 'rm -rf \"$S\" /opt/diskos.new' EXIT\n" +
                    "tar -xzf " + Env.Q(Env.ToLinux(src)) + " -C \"$S\"; R=$(echo \"$S\"/*)\n" +
                    "want=$(awk '$2==\"mq_ui\"||$2==\"*mq_ui\"{print $1}' " + Env.Q(Env.ToLinux(sums)) + ")\n" +
                    "got=$(sha256sum \"$R/payload/mq_ui\" | cut -d' ' -f1)\n" +
                    "[ -n \"$want\" ] && [ \"$want\" = \"$got\" ] || { echo 'The release files do not match their checksums; nothing was changed.'; exit 3; }\n" +
                    "D=/opt/diskos; N=/opt/diskos.new; rm -rf \"$N\"; mkdir -p \"$N/tools\"\n" +
                    "cp -a \"$R/diskos-installer\" \"$R/diskos_installer\" \"$R/payload\" \"$R/requirements.txt\" \"$R/LICENSE\" \"$R/NOTICE.md\" \"$R/licenses\" \"$N/\"\n" +
                    "cp \"$R/ui/tools/owner-keys/diskos-keys.sh\" \"$R/ui/tools/owner-keys/diskos-sign.sh\" \"$N/tools/\"\n" +
                    "PARTS='diskos-installer diskos_installer payload requirements.txt LICENSE NOTICE.md licenses tools'\n" +
                    "for x in $PARTS; do rm -rf \"$D/$x.old\"; [ -e \"$D/$x\" ] && mv \"$D/$x\" \"$D/$x.old\"; mv \"$N/$x\" \"$D/$x\"; done\n" +
                    "if ! cmp -s \"$D/requirements.txt.old\" \"$D/requirements.txt\"; then\n" +
                    "  if ! \"$D/.venv/bin/pip\" install -q --no-cache-dir -r \"$D/requirements.txt\"; then\n" +
                    "    for x in $PARTS; do rm -rf \"$D/$x\"; [ -e \"$D/$x.old\" ] && mv \"$D/$x.old\" \"$D/$x\"; done\n" +
                    "    echo 'Could not install the new Python requirements; kept the previous installer files.'; exit 4\n" +
                    "  fi\n" +
                    "fi\n" +
                    "for x in $PARTS; do rm -rf \"$D/$x.old\"; done\n" +
                    "UI=$(sed -n 's/.*\"version\": \"\\(.*\\)\".*/\\1/p' \"$D/payload/mq_ui.build.json\" | head -1)\n" +
                    "[ -f \"$D/WSL_ENV_VERSION\" ] && sed -i \"s/ ui-[^ ]*$/ ui-$UI/\" \"$D/WSL_ENV_VERSION\"\n" +
                    "echo " + Env.Q(tag) + " > \"$D/RELEASE_TAG\"\n" +
                    "echo \"Installer files updated to " + tag + " (interface $UI).\"\n", out o);
                return (o ?? "").Trim().Length > 0 ? o.Trim() : (rc == 0 ? "Installer files updated to " + tag + "." : "Updating the installer files failed (code " + rc + "); nothing was changed.");
            }
            catch (Exception e) { return "Couldn't download the latest release (" + e.Message + "); using the installer files already here."; }
        }

        // "v2.0.0" from github.com/.../releases/latest (it redirects to .../releases/tag/v2.0.0)
        public static string Tag()
        {
            try
            {
                var req = (HttpWebRequest)WebRequest.Create(Repo + "/latest");
                req.AllowAutoRedirect = false; req.Method = "HEAD"; req.UserAgent = "diskOS-Disco-for-Windows";
                using (var r = (HttpWebResponse)req.GetResponse())
                {
                    string loc = r.Headers["Location"] ?? "";
                    int i = loc.LastIndexOf("/tag/", StringComparison.Ordinal);
                    return i >= 0 ? Uri.UnescapeDataString(loc.Substring(i + 5)) : null;
                }
            }
            catch { return null; }
        }
    }
}
