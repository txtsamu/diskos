// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - shown at every start: what this is, and what it isn't.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace DiskosDisco
{
    class Disclaimer : Form
    {
        public Disclaimer()
        {
            Theme.Style(this);
            Text = "Before you start - diskOS Disco! for Windows";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(600, 700);
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint, true);

            var body = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true,
                Location = new Point(36, 122), Size = new Size(536, 490), BackColor = Color.Transparent,
            };
            Controls.Add(body);

            Para(body, "diskOS Disco! replaces the interface of your FiiO Snowsky Disc with an unofficial one. Please read this first.", Theme.Text);
            Point(body, "Unofficial.", "A hobby project, not made, supported or endorsed by FiiO, Snowsky or Ingenic. Installing it may affect your warranty.");
            Point(body, "Unsupported, but working as intended.", "Users have confirmed several times that installing and SD updates work from Windows. There is no help for Windows-specific problems, though.");
            Point(body, "Test software, as-is.", "It is a testing release, provided without any warranty. Something can always go wrong when a device is flashed, and you use it at your own risk.");
            Point(body, "Start from stock V2.57.", "Install on FiiO's stock V2.57 firmware. Installing over upstream diskOS or another diskOS build is not supported and can be buggy.");
            Point(body, "Switching between diskOS builds.", "Going back to stock does not clear settings that diskOS left on the Disc. If Disco looks or behaves wrong after switching, use Settings > System > Maintenance > Reset diskOS Settings (music, Wi-Fi, Bluetooth, EQ and audio settings stay).");
            Point(body, "Your firmware, your keys.", "You supply FiiO's official firmware file yourself. Your update keys stay on this PC: back them up, because the Disc only accepts updates signed with them.");
            Point(body, "There is a way back.", "A backup of your Disc's original system is saved before the install, and the stock interface stays one setting away. Keep the Disc charged and connected while flashing.");
            Point(body, "Changes to this PC.", "It uses Microsoft's WSL and the usbipd-win USB bridge, and sets up its own small Linux environment, which you can remove again from inside the app.");

            var ok = new PillButton("I understand, continue", true) { Location = new Point(ClientSize.Width - 36 - 240, ClientSize.Height - 66) };
            ok.Click += (s, e) => { DialogResult = DialogResult.OK; Close(); };
            var quit = new PillButton("Quit", false) { Location = new Point(36, ClientSize.Height - 66) };
            quit.Click += (s, e) => { DialogResult = DialogResult.Cancel; Close(); };
            Controls.Add(ok); Controls.Add(quit);
            AcceptButton = ok; CancelButton = quit;
            Shown += (s, e) => { ok.Location = new Point(ClientSize.Width - 36 - ok.Width, ok.Top); ok.Focus(); };
        }

        static void Para(FlowLayoutPanel body, string text, Color color)
        {
            body.Controls.Add(new Label { Text = text, AutoSize = true, MaximumSize = new Size(510, 0), ForeColor = color, Margin = new Padding(0, 0, 0, 12) });
        }

        static void Point(FlowLayoutPanel body, string head, string text)
        {
            body.Controls.Add(new Label { Text = head, AutoSize = true, ForeColor = Theme.Text, Font = new Font("Segoe UI Semibold", 10f), Margin = new Padding(0, 2, 0, 0) });
            body.Controls.Add(new Label { Text = text, AutoSize = true, MaximumSize = new Size(510, 0), ForeColor = Theme.Muted, Margin = new Padding(0, 0, 0, 10) });
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            using (var b = new LinearGradientBrush(new Rectangle(0, 0, Width, 100), Color.FromArgb(30, 16, 50), Theme.Bg, 90f)) g.FillRectangle(b, 0, 0, Width, 100);
            using (var f = new Font("Segoe UI", 9f, FontStyle.Bold)) using (var t = new SolidBrush(Theme.Orange))
                g.DrawString("BEFORE YOU START", f, t, 38, 30);
            using (var f = new Font("Segoe UI", 18f, FontStyle.Bold)) using (var t = new SolidBrush(Theme.Text))
                g.DrawString("Good to know", f, t, 36, 50);
            using (var b = Theme.Rainbow(new RectangleF(0, 98, Width, 2))) g.FillRectangle(b, 0, 98, Width, 2);
        }
    }
}
