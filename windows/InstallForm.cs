// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - step 3 page: firmware, update keys, connect the Disc, install.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace DiskosDisco
{
    class InstallForm : Form
    {
        public bool WentBack;

        [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
        const uint ES_CONTINUOUS = 0x80000000, ES_SYSTEM_REQUIRED = 0x00000001;

        readonly Label fwLabel, keysLabel, discLabel, status;
        readonly RadioButton keysNew, keysOld;
        readonly Button fwButton, keysButton, findButton, installButton, back, openKeys;
        readonly ProgressBar progress;
        readonly TextBox log;
        string firmware, keysDir, busid;
        bool discReady, busy;
        Process keepAlive, attach;

        static string DefaultKeys
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "diskOS Keys"); }
        }

        public InstallForm()
        {
            Text = "diskOS Disco! for Windows";
            Font = new Font("Segoe UI", 10f);
            BackColor = Color.White;
            AutoScaleMode = AutoScaleMode.Dpi;
            MinimumSize = new Size(680, 600);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 22, 28, 18), ColumnCount = 1, RowCount = 7 };
            for (int i = 0; i < 5; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root);

            var head = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false };
            head.Controls.Add(new Label { Text = "Step 3 of 4 · Install", ForeColor = MainForm.Accent, AutoSize = true, Font = new Font(Font, FontStyle.Bold) });
            head.Controls.Add(new Label { Text = "Install diskOS Disco! on your Disc", ForeColor = MainForm.Ink, AutoSize = true, Font = new Font("Segoe UI Semibold", 16f), Margin = new Padding(0, 4, 0, 10) });
            root.Controls.Add(head, 0, 0);

            // 1. Firmware
            fwButton = MainForm.MakeButton("Choose…", false);
            fwButton.Click += (s, e) => ChooseFirmware();
            fwLabel = Hint("FiiO's official update ZIP for firmware V2.57 (SNOWSKY_DISC_update_…_v257.zip). Your Disc's own " +
                           "system is built from it; diskOS never shares or uploads it.");
            root.Controls.Add(Section("1.  Firmware", fwLabel, fwButton), 0, 1);

            // 2. Keys
            keysNew = new RadioButton { Text = "Create new update keys (first install)", AutoSize = true, Checked = true };
            keysOld = new RadioButton { Text = "Use my existing keys folder", AutoSize = true };
            keysButton = MainForm.MakeButton("Choose…", false);
            keysButton.Click += (s, e) => ChooseKeys();
            keysLabel = Hint("");
            keysNew.CheckedChanged += (s, e) => UpdateKeys();
            var keyRow = new FlowLayoutPanel { AutoSize = true, WrapContents = true, Margin = new Padding(0) };
            keyRow.Controls.Add(keysNew); keyRow.Controls.Add(keysOld);
            var keyBox = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            keyBox.Controls.Add(keyRow, 0, 0); keyBox.Controls.Add(keysLabel, 0, 1);
            root.Controls.Add(Section("2.  Update keys", keyBox, keysButton), 0, 2);

            // 3. Connect
            findButton = MainForm.MakeButton("Find my Disc", false);
            findButton.Click += (s, e) => FindDisc();
            discLabel = Hint("Turn the Disc off. Hold Volume Down and connect it to this PC with a USB data cable, then " +
                             "click Find my Disc. The screen stays black: that's normal.");
            root.Controls.Add(Section("3.  Connect the Disc", discLabel, findButton), 0, 3);

            // Install
            var go = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 14, 0, 0) };
            installButton = MainForm.MakeButton("Install", true);
            installButton.Click += (s, e) => Install();
            progress = new ProgressBar { Style = ProgressBarStyle.Marquee, Width = 180, Visible = false, Margin = new Padding(12, 12, 0, 0) };
            status = new Label { AutoSize = true, ForeColor = MainForm.Ink, Margin = new Padding(12, 11, 0, 0), MaximumSize = new Size(420, 0) };
            go.Controls.Add(installButton); go.Controls.Add(progress); go.Controls.Add(status);
            root.Controls.Add(go, 0, 4);

            log = new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, WordWrap = false,
                Font = new Font("Consolas", 9f), BackColor = Color.FromArgb(246, 246, 248), BorderStyle = BorderStyle.None,
                Margin = new Padding(0, 12, 0, 0)
            };
            root.Controls.Add(log, 0, 5);

            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
            back = MainForm.MakeButton("Back", false);
            back.Click += (s, e) => { WentBack = true; Close(); };
            var copy = MainForm.MakeButton("Copy report", false);
            copy.Click += (s, e) => { try { Clipboard.SetText("diskOS Disco! for Windows " + Program.Version + "\r\n" + status.Text + "\r\n\r\n" + log.Text); } catch { } };
            openKeys = MainForm.MakeButton("Open keys folder", false);
            openKeys.Click += (s, e) => { if (keysDir != null && Directory.Exists(keysDir)) Sys.Open(keysDir); };
            bar.Controls.Add(back); bar.Controls.Add(copy); bar.Controls.Add(openKeys);
            root.Controls.Add(bar, 0, 6);

            // Keys made by an earlier run are found again.
            if (File.Exists(Path.Combine(DefaultKeys, "diskos-root.pub.pem"))) { keysOld.Checked = true; keysDir = DefaultKeys; }
            UpdateKeys();
            UpdateButtons();

            FormClosing += (s, e) =>
            {
                if (busy) { e.Cancel = true; MessageBox.Show(this, "Please wait: the Disc is being installed. Don't unplug it.", Text); return; }
                Release();
            };
        }

        Label Hint(string text)
        {
            return new Label { Text = text, AutoSize = true, ForeColor = MainForm.Muted, MaximumSize = new Size(480, 0), Margin = new Padding(0, 2, 0, 0) };
        }

        Control Section(string title, Control body, Button action)
        {
            var t = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 12) };
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var box = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            box.Controls.Add(new Label { Text = title, AutoSize = true, ForeColor = MainForm.Ink, Font = new Font(Font, FontStyle.Bold) }, 0, 0);
            box.Controls.Add(body, 0, 1);
            t.Controls.Add(box, 0, 0);
            action.Margin = new Padding(12, 4, 0, 0);
            t.Controls.Add(action, 1, 0);
            return t;
        }

        void Append(string line)
        {
            if (InvokeRequired) { BeginInvoke(new Action<string>(Append), line); return; }
            log.AppendText((log.TextLength > 0 ? "\r\n" : "") + line);
        }

        void Say(string text, Color color)
        {
            if (InvokeRequired) { BeginInvoke(new Action<string, Color>(Say), text, color); return; }
            status.Text = text; status.ForeColor = color;
        }

        void UpdateButtons()
        {
            bool keysOk = keysNew.Checked || (keysDir != null && File.Exists(Path.Combine(keysDir, "diskos-root.pub.pem")));
            fwButton.Enabled = keysButton.Enabled = findButton.Enabled = keysNew.Enabled = keysOld.Enabled = !busy;
            back.Enabled = !busy;
            installButton.Enabled = !busy && firmware != null && keysOk && discReady;
            openKeys.Visible = keysDir != null && Directory.Exists(keysDir);
            progress.Visible = busy;
        }

        void ChooseFirmware()
        {
            using (var d = new OpenFileDialog { Title = "Choose the Snowsky Disc firmware ZIP", Filter = "Firmware ZIP (*.zip)|*.zip" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                if (Env.ToLinux(d.FileName) == null)
                {
                    MessageBox.Show(this, "Please copy the ZIP to this PC first (for example to Downloads) and choose it there.", Text);
                    return;
                }
                firmware = d.FileName;
                fwLabel.Text = "✔  " + Path.GetFileName(firmware);
                fwLabel.ForeColor = MainForm.Good;
                UpdateButtons();
            }
        }

        void ChooseKeys()
        {
            using (var d = new FolderBrowserDialog { Description = "Choose the folder with your diskOS keys (it contains diskos-root.pub.pem)" })
            {
                if (keysDir != null) d.SelectedPath = keysDir;
                if (d.ShowDialog(this) != DialogResult.OK) return;
                keysDir = d.SelectedPath;
                keysOld.Checked = true;
                UpdateKeys();
            }
        }

        void UpdateKeys()
        {
            if (keysNew.Checked)
            {
                if (File.Exists(Path.Combine(DefaultKeys, "root.key")))
                {
                    keysLabel.Text = "There are already keys in " + DefaultKeys + ". Use them instead (a Disc only accepts updates from the keys it was installed with).";
                    keysLabel.ForeColor = MainForm.Bad;
                }
                else
                {
                    keysLabel.Text = "Made in Documents\\diskOS Keys during the install. They sign your future updates; you'll be asked to back them up.";
                    keysLabel.ForeColor = MainForm.Muted;
                }
            }
            else if (keysDir == null)
            {
                keysLabel.Text = "Click Choose… and pick the folder with diskos-root.pub.pem (the keys you installed with before).";
                keysLabel.ForeColor = MainForm.Muted;
            }
            else if (File.Exists(Path.Combine(keysDir, "diskos-root.pub.pem")))
            {
                keysLabel.Text = "✔  " + keysDir; keysLabel.ForeColor = MainForm.Good;
            }
            else
            {
                keysLabel.Text = "✖  No diskos-root.pub.pem in " + keysDir + "."; keysLabel.ForeColor = MainForm.Bad;
            }
            UpdateButtons();
        }

        // Find the Disc in mask-ROM mode, share it with WSL (administrator, once) and attach it to diskOS's environment.
        void FindDisc()
        {
            busy = true; discReady = false; UpdateButtons();
            Say("Looking for the Disc…", MainForm.Ink);
            new Thread(() =>
            {
                string msg; bool ok = false;
                try { ok = Connect(out msg); } catch (Exception e) { msg = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    busy = false; discReady = ok;
                    discLabel.Text = (ok ? "✔  " : "✖  ") + msg;
                    discLabel.ForeColor = ok ? MainForm.Good : MainForm.Bad;
                    status.Text = "";
                    UpdateButtons();
                }));
            }) { IsBackground = true }.Start();
        }

        bool Connect(out string msg)
        {
            string usbipd = UsbBridgeCheck.Find(), o;
            if (usbipd == null) { msg = "The USB bridge (usbipd-win) isn't installed. Go Back to step 1."; return false; }
            Sys.Run(usbipd, "list", out o);
            string line = null;
            foreach (string l in o.Split('\n')) if (l.ToLowerInvariant().Contains("a108:eaef")) { line = l.Trim(); break; }
            if (line == null)
            {
                msg = "No Disc found. Turn it off, hold Volume Down, then plug in the cable (a data cable, straight into the PC), and try again.";
                return false;
            }
            busid = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)[0];
            Append("Disc found on USB " + busid + ": " + line);
            if (line.Contains("Not shared"))
            {
                Say("Windows will ask for permission to share the Disc with diskOS…", MainForm.Ink);
                int rc;
                if (!Sys.RunAdmin(usbipd, "bind --busid " + busid, out rc)) { msg = "Sharing needs administrator permission. Click Find my Disc and answer Yes."; return false; }
                if (rc != 0) { msg = "Couldn't share the Disc with diskOS (code " + rc + ")."; return false; }
                Append("Shared with WSL.");
            }
            if (keepAlive == null || keepAlive.HasExited) keepAlive = Env.KeepAlive();
            Thread.Sleep(1500);
            if (attach == null || attach.HasExited) attach = Sys.Background(usbipd, "attach --wsl --busid " + busid + " --auto-attach");
            Say("Connecting the Disc to diskOS…", MainForm.Ink);
            for (int i = 0; i < 20; i++)
            {
                Thread.Sleep(1000);
                if (Env.Script("/opt/diskos/.venv/bin/python -c \"import sys, usb.core; sys.exit(0 if usb.core.find(idVendor=0xa108, idProduct=0xeaef) else 1)\"", out o) == 0)
                {
                    msg = "Disc connected and ready.";
                    Append("Disc attached to diskOS's environment.");
                    return true;
                }
            }
            msg = "The Disc was found but didn't reach diskOS. Unplug it, turn it off, hold Volume Down, plug it in again and click Find my Disc.";
            return false;
        }

        void Release()
        {
            try { if (attach != null && !attach.HasExited) attach.Kill(); } catch { }
            try { if (keepAlive != null && !keepAlive.HasExited) keepAlive.Kill(); } catch { }
            string usbipd = UsbBridgeCheck.Find(), o;
            if (usbipd != null && busid != null) Sys.Run(usbipd, "detach --busid " + busid, out o);
        }

        void Install()
        {
            string keys = keysNew.Checked ? DefaultKeys : keysDir;
            if (keysNew.Checked && File.Exists(Path.Combine(keys, "root.key")))
            {
                MessageBox.Show(this, "There are already keys in " + keys + ". Choose \"Use my existing keys folder\".", Text);
                return;
            }
            string what = "Install diskOS Disco! on the connected Disc?\n\n" +
                          "Firmware: " + Path.GetFileName(firmware) + "\n" +
                          "Update keys: " + (keysNew.Checked ? "new, in " + keys : keys) + "\n\n" +
                          "This takes about 20 minutes. Keep the Disc connected and this PC on until it says Done. " +
                          "A backup of the Disc's original system is kept, so you can go back to stock later.";
            if (MessageBox.Show(this, what, Text, MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;

            busy = true; UpdateButtons();
            log.Clear();
            SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);   // no sleep while flashing
            bool makeKeys = keysNew.Checked;
            string fw = Env.ToLinux(firmware), kl = Env.ToLinux(keys);
            new Thread(() =>
            {
                bool ok = false; string end;
                try { ok = RunInstall(makeKeys, fw, kl, out end); }
                catch (Exception e) { end = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    SetThreadExecutionState(ES_CONTINUOUS);
                    busy = false;
                    keysDir = keys; keysOld.Checked = true; UpdateKeys();
                    Say(end, ok ? MainForm.Good : MainForm.Bad);
                    UpdateButtons();
                    if (ok)
                        MessageBox.Show(this,
                            "Done! Unplug the Disc and turn it on. diskOS Disco! installs itself on the first start.\n\n" +
                            "Important: copy the \"diskOS Keys\" folder (in Documents) to a USB stick or another safe place. " +
                            "Your Disc only accepts updates signed with these keys.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }));
            }) { IsBackground = true }.Start();
        }

        bool RunInstall(bool makeKeys, string fw, string keys, out string end)
        {
            string o;
            if (makeKeys)
            {
                Say("Making your update keys…", MainForm.Ink);
                int k = Env.Script("rm -rf /tmp/diskos-keys\nsh /opt/diskos/tools/diskos-keys.sh /tmp/diskos-keys\n" +
                                   "mkdir -p " + Env.Q(keys) + "\ncp /tmp/diskos-keys/* " + Env.Q(keys) + "/\nrm -rf /tmp/diskos-keys", Append);
                if (k != 0) { end = "Making the keys failed (code " + k + ")."; return false; }
            }
            Say("Installing… about 20 minutes. Keep the Disc connected.", MainForm.Ink);
            int rc = Env.Script("diskos install --variant public --yes --firmware " + Env.Q(fw) +
                                " --ota-key " + Env.Q(keys + "/diskos-root.pub.pem"), line =>
            {
                Append(line);
                string t = line.Trim();
                if (t.StartsWith("▶")) Say(t.TrimStart('▶', ' ') + "…", MainForm.Ink);
            });
            Env.SaveBackup(out o); Append(o);
            if (rc == 0) { end = "Done. Unplug the Disc and turn it on."; return true; }
            end = rc == 3 ? "Stopped before anything was written to the Disc." : "The install didn't finish (code " + rc + "). Copy report and send it.";
            return false;
        }
    }
}
