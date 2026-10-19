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
