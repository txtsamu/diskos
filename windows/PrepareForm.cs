// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - step 2 page: set up diskOS's own Linux environment and prove it works.
using System;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;

namespace DiskosDisco
{
    class PrepareForm : Form
    {
        public bool WentBack;

        readonly Label status, detail;
        readonly ProgressBar progress;
        readonly TextBox log;
        readonly Button prepare, test, remove, back, next;
        bool busy;

        public PrepareForm()
        {
            Text = "diskOS Disco! for Windows";
            Font = new Font("Segoe UI", 10f);
            BackColor = Color.White;
            AutoScaleMode = AutoScaleMode.Dpi;
            MinimumSize = new Size(640, 520);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 22, 28, 18), ColumnCount = 1, RowCount = 5 };
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root);

            var head = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false, Dock = DockStyle.Fill };
            head.Controls.Add(new Label { Text = "Step 2 of 4 · Prepare", ForeColor = MainForm.Accent, AutoSize = true, Font = new Font(Font, FontStyle.Bold) });
            head.Controls.Add(new Label { Text = "diskOS's own Linux environment", ForeColor = MainForm.Ink, AutoSize = true, Font = new Font("Segoe UI Semibold", 16f), Margin = new Padding(0, 4, 0, 6) });
            head.Controls.Add(new Label
            {
                Text = "diskOS builds the Disc's new firmware in a small, private Linux environment inside WSL. Setting it " +
                       "up takes a minute or two and about 150 MB. It is separate from anything else in WSL on this PC, " +
                       "and can be removed again here.",
                ForeColor = MainForm.Muted, MaximumSize = new Size(690, 0), AutoSize = true, Margin = new Padding(0, 0, 0, 14)
            });
            root.Controls.Add(head, 0, 0);

            var state = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false };
            status = new Label { AutoSize = true, ForeColor = MainForm.Ink, Font = new Font("Segoe UI Semibold", 12f) };
            detail = new Label { AutoSize = true, ForeColor = MainForm.Muted, MaximumSize = new Size(690, 0), Margin = new Padding(0, 4, 0, 8) };
            state.Controls.Add(status); state.Controls.Add(detail);
            root.Controls.Add(state, 0, 1);

            var actions = new FlowLayoutPanel { AutoSize = true, WrapContents = true };
            prepare = MainForm.MakeButton("Set up", true); prepare.Margin = new Padding(0, 4, 8, 0);
            prepare.Click += (s, e) => Prepare();
            test = MainForm.MakeButton("Test it", false); test.Margin = new Padding(0, 4, 8, 0);
            test.Click += (s, e) => Test();
            remove = MainForm.MakeButton("Remove it", false); remove.Margin = new Padding(0, 4, 8, 0);
            remove.Click += (s, e) => Remove();
            progress = new ProgressBar { Style = ProgressBarStyle.Marquee, Width = 200, Visible = false, Margin = new Padding(8, 10, 0, 0) };
            actions.Controls.Add(prepare); actions.Controls.Add(test); actions.Controls.Add(remove); actions.Controls.Add(progress);
            root.Controls.Add(actions, 0, 2);

            log = new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, WordWrap = false,
                Font = new Font("Consolas", 9f), BackColor = Color.FromArgb(246, 246, 248), BorderStyle = BorderStyle.None,
                Margin = new Padding(0, 14, 0, 0)
            };
            root.Controls.Add(log, 0, 3);

            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
            next = MainForm.MakeButton("Next", true);
            next.Click += (s, e) => MessageBox.Show(this,
                "diskOS's environment is ready.\n\nThis test build stops here. The next one adds the install wizard: " +
                "firmware, your update keys, connecting the Disc and flashing.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            back = MainForm.MakeButton("Back", false);
            back.Click += (s, e) => { WentBack = true; Close(); };
            var copy = MainForm.MakeButton("Copy report", false);
            copy.Click += (s, e) => { try { Clipboard.SetText("diskOS Disco! for Windows " + Program.Version + "\r\n" + status.Text + "\r\n" + detail.Text + "\r\n\r\n" + log.Text); } catch { } };
            bar.Controls.Add(next); bar.Controls.Add(back); bar.Controls.Add(copy);
            root.Controls.Add(bar, 0, 4);

            FormClosing += (s, e) => { if (busy) { e.Cancel = true; MessageBox.Show(this, "Please wait until this step finishes.", Text); } };
            Shown += (s, e) => Look(null);
        }

        // Runs work off the UI thread, with the buttons locked and the progress bar moving.
        void Work(string doing, Func<string> job)
        {
            busy = true;
            prepare.Enabled = test.Enabled = remove.Enabled = back.Enabled = next.Enabled = false;
            progress.Visible = true;
            status.Text = doing; status.ForeColor = MainForm.Ink;
            new Thread(() =>
            {
                string text;
                try { text = job(); } catch (Exception ex) { text = "Something went wrong: " + ex.Message; }
                BeginInvoke(new Action(() => { busy = false; progress.Visible = false; Look(text); }));
            }) { IsBackground = true }.Start();
        }

        void Append(string text)
        {
            if (string.IsNullOrEmpty(text)) return;
            log.AppendText((log.TextLength > 0 ? "\r\n\r\n" : "") + text.Replace("\r\n", "\n").Replace("\n", "\r\n"));
        }

        // Look at what's there and set the buttons to match.
        void Look(string result)
        {
            Append(result);
            prepare.Enabled = test.Enabled = remove.Enabled = false; back.Enabled = true; next.Enabled = false;
            status.Text = "Looking…"; detail.Text = "";
            busy = true;
            new Thread(() =>
            {
                bool exists = Env.Exists();
                string version = exists ? Env.Version() : null;
                string package = Env.FindPackage();
                BeginInvoke(new Action(() =>
                {
                    busy = false;
                    if (exists && version != null)
                    {
                        status.Text = "✔  Ready"; status.ForeColor = MainForm.Good;
                        detail.Text = "diskOS's environment is set up (" + version + "). Test it if you like, then click Next.";
                        test.Enabled = remove.Enabled = next.Enabled = true;
                        prepare.Text = "Set up again"; prepare.Enabled = package != null;
                    }
                    else if (exists)
                    {
                        status.Text = "✖  Not working"; status.ForeColor = MainForm.Bad;
                        detail.Text = "The environment exists but doesn't answer. Remove it and set it up again.";
                        remove.Enabled = true;
                    }
                    else if (package != null)
                    {
                        status.Text = "Not set up yet"; status.ForeColor = MainForm.Ink;
                        detail.Text = "Click Set up.";
                        prepare.Text = "Set up"; prepare.Enabled = true;
                    }
                    else
                    {
                        status.Text = "✖  Package missing"; status.ForeColor = MainForm.Bad;
                        detail.Text = "Put " + Env.Package + " in the same folder as this app (it comes in the same ZIP), then come back to this page.";
                    }
                }));
            }) { IsBackground = true }.Start();
        }

        void Prepare()
        {
            string package = Env.FindPackage();
            if (package == null) { Look(null); return; }
            bool again = Env.Exists();
            if (again && MessageBox.Show(this, "Set up diskOS's environment again from scratch?", Text, MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            Work("Setting up… (a minute or two)", () =>
            {
                string o, text = "";
                if (again) { Env.Remove(out o); text += "Removed the old environment.\n"; }
                int rc = Env.Import(package, out o);
                if (rc != 0) return text + "Setting up failed (code " + rc + "):\n" + o;
                rc = Env.Run("diskos doctor", out o);
                return text + "Set up.\n\n" + o + (rc == 0 ? "" : "\n\nThe test reported a problem (code " + rc + ").");
            });
        }

        void Test()
        {
            Work("Testing…", () =>
            {
                string o;
                int rc = Env.Run("diskos doctor", out o);
                return o + (rc == 0 ? "\n\nTest passed." : "\n\nThe test reported a problem (code " + rc + ").");
            });
        }

        void Remove()
        {
            if (MessageBox.Show(this, "Remove diskOS's environment from this PC? You can set it up again any time.",
                                Text, MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            Work("Removing…", () =>
            {
                string o;
                int rc = Env.Remove(out o);
                return rc == 0 ? "Removed." : "Removing failed (code " + rc + "):\n" + o;
            });
        }
    }
}
