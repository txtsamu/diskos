// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - About: who made this possible.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace DiskosDisco
{
    class AboutForm : Form
    {
        readonly Bitmap disc = Theme.Disc(96);

        public AboutForm()
        {
            Theme.Style(this);
            Text = "About diskOS Disco! for Windows";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(580, 740);
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint, true);

            var body = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true,
                Location = new Point(36, 190), Size = new Size(520, 470), BackColor = Color.Transparent,
            };
            Controls.Add(body);

            Add(body, "diskOS Disco! is a touch interface for the FiiO Snowsky Disc's round screen. This app installs it " +
                      "from Windows.", Theme.Muted, 0);

            Head(body, "Thank you");
            Credit(body, "b0hemia", "https://github.com/b0hemia/diskos",
                   "for diskOS itself: the reverse engineering, the installer, the boot and recovery chain and the signed " +
                   "updates. None of this exists without that work.");
            Link(body, "Support b0hemia on Ko-fi", "https://ko-fi.com/b0hemia");
            Credit(body, "ingvarr777", "https://github.com/ingvarr777",
                   "for Ogg Opus support and the careful bug and security reports behind 1.4.1.");
            Credit(body, "diskOS contributors", "https://github.com/b0hemia/diskos",
                   "everyone who reported, tested and patched, upstream and here.");
            Credit(body, "Open source", null,
                   "WSL and usbipd-win (Microsoft, dorssel), Ubuntu, Python, LVGL, SQLite, musl, OpenSSL, libusb, " +
                   "squashfs-tools and every project listed in NOTICE.md.");
            Add(body, "Not affiliated with or endorsed by FiiO, Snowsky or Ingenic.", Theme.Faint, 10);

            var close = new PillButton("Close", false) { Location = new Point(ClientSize.Width - 130, ClientSize.Height - 56) };
            close.Click += (s, e) => Close();
            Controls.Add(close);
            CancelButton = close;
            var site = new PillButton("Website", false) { Location = new Point(36, ClientSize.Height - 56) };
            site.Click += (s, e) => Sys.Open("https://zmd22.github.io/diskos-disco/");
            Controls.Add(site);
        }

        static void Add(FlowLayoutPanel body, string text, Color color, int top)
        {
            body.Controls.Add(new Label { Text = text, AutoSize = true, MaximumSize = new Size(470, 0), ForeColor = color, Margin = new Padding(0, top, 0, 6) });
        }

        static void Head(FlowLayoutPanel body, string text)
        {
            body.Controls.Add(new Label { Text = text.ToUpperInvariant(), AutoSize = true, ForeColor = Theme.Cyan, Font = new Font("Segoe UI", 9f, FontStyle.Bold), Margin = new Padding(0, 12, 0, 6) });
        }

        static void Link(FlowLayoutPanel body, string text, string url)
        {
            var l = new LinkLabel { Text = text, AutoSize = true, LinkColor = Theme.Pink, ActiveLinkColor = Theme.Orange, VisitedLinkColor = Theme.Pink, Margin = new Padding(0, 0, 0, 8) };
            l.Click += (s, e) => Sys.Open(url);
            body.Controls.Add(l);
        }

        static void Credit(FlowLayoutPanel body, string who, string url, string why)
        {
            var name = new LinkLabel
            {
                Text = who, AutoSize = true, Font = new Font("Segoe UI Semibold", 10.5f), Margin = new Padding(0, 4, 0, 0),
                LinkColor = Theme.Text, ActiveLinkColor = Theme.Cyan, VisitedLinkColor = Theme.Text, LinkBehavior = LinkBehavior.HoverUnderline,
            };
            if (url != null) name.Click += (s, e) => Sys.Open(url); else name.LinkArea = new LinkArea(0, 0);
            if (url == null) name.ForeColor = Theme.Text;
            body.Controls.Add(name);
            Add(body, why, Theme.Muted, 0);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            using (var b = new LinearGradientBrush(new Rectangle(0, 0, Width, 200), Color.FromArgb(30, 16, 50), Theme.Bg, 90f)) g.FillRectangle(b, 0, 0, Width, 200);
            g.DrawImage(disc, 36 - (disc.Width - 96) / 2, 30 - (disc.Height - 96) / 2);
            using (var f = new Font("Segoe UI", 22f, FontStyle.Bold)) Theme.Wordmark(g, f, 152, 44);
            using (var f = new Font("Segoe UI", 10f)) using (var t = new SolidBrush(Theme.Muted))
                g.DrawString("for Windows · " + Program.ShortVersion, f, t, 154, 86);
            using (var f = new Font("Segoe UI", 16f, FontStyle.Bold | FontStyle.Italic))
            {
                var sz = g.MeasureString("Happy listening!", f);
                var r = new RectangleF(152, 118, sz.Width, sz.Height);
                using (var b = Theme.Rainbow(r)) g.DrawString("Happy listening!", f, b, r.X, r.Y);
            }
        }
    }
}
