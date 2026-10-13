// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - the look: the website's night colours, the CD-rainbow disc and pill buttons.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace DiskosDisco
{
    static class Theme
    {
        public static readonly Color Bg = Color.FromArgb(11, 11, 16), Bg2 = Color.FromArgb(18, 18, 26), Card = Color.FromArgb(24, 24, 34);
        public static readonly Color Text = Color.FromArgb(242, 241, 246), Muted = Color.FromArgb(166, 164, 180), Faint = Color.FromArgb(111, 109, 126);
        public static readonly Color Line = Color.FromArgb(44, 44, 58);
        public static readonly Color Pink = Color.FromArgb(255, 90, 160), Orange = Color.FromArgb(255, 180, 90), Yellow = Color.FromArgb(255, 240, 106);
        public static readonly Color Green = Color.FromArgb(98, 242, 140), Cyan = Color.FromArgb(74, 216, 255), Blue = Color.FromArgb(106, 140, 255);
        public static readonly Color Violet = Color.FromArgb(179, 107, 255), Red = Color.FromArgb(255, 96, 112);

        static readonly Color[] Iri = { Pink, Orange, Yellow, Green, Cyan, Blue, Violet, Pink };

        public static Color Mix(Color a, Color b, float t)
        {
            return Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }

        // The rainbow at 0..1 along the iridescent sweep.
        public static Color IriAt(float t)
        {
            t = (t % 1f + 1f) % 1f * (Iri.Length - 1);
            int i = Math.Min((int)t, Iri.Length - 2);
            return Mix(Iri[i], Iri[i + 1], t - i);
        }

        // The disc: an iridescent CD with a dark hub and a soft glow. Drawn once per size.
        public static Bitmap Disc(int size)
        {
            int pad = size / 5, full = size + pad * 2;
            var bmp = new Bitmap(full, full);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                for (int r = pad; r > 0; r -= 2)                                       // glow
                    using (var b = new SolidBrush(Color.FromArgb((int)(36f * (1f - (float)r / pad)), Violet)))
                        g.FillEllipse(b, pad - r, pad - r, size + r * 2, size + r * 2);
                var rect = new RectangleF(pad, pad, size, size);
                for (int a = 0; a < 360; a += 2)                                       // the conic rainbow
                    using (var b = new SolidBrush(IriAt((a + 210) / 360f)))
                        g.FillPie(b, rect.X, rect.Y, rect.Width, rect.Height, a - 90, 2.6f);
                using (var b = new PathGradientBrush(Ellipse(rect)))                    // a lighter centre, like a real CD
                {
                    b.CenterColor = Color.FromArgb(70, 255, 255, 255);
                    b.SurroundColors = new[] { Color.FromArgb(0, 255, 255, 255) };
                    g.FillEllipse(b, rect);
                }
                float hub = size * 0.36f, hole = size * 0.14f;
                using (var b = new SolidBrush(Color.FromArgb(150, Bg))) g.FillEllipse(b, pad + (size - hub) / 2, pad + (size - hub) / 2, hub, hub);
                using (var b = new SolidBrush(Bg)) g.FillEllipse(b, pad + (size - hole) / 2, pad + (size - hole) / 2, hole, hole);
                using (var p = new Pen(Color.FromArgb(60, 255, 255, 255), Math.Max(1f, size / 60f))) g.DrawEllipse(p, rect);
            }
            return bmp;
        }

        static GraphicsPath Ellipse(RectangleF r) { var p = new GraphicsPath(); p.AddEllipse(r); return p; }

        public static GraphicsPath Pill(RectangleF r)
        {
            float d = r.Height;
            var p = new GraphicsPath();
            p.AddArc(r.X, r.Y, d, d, 90, 180);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 180);
            p.CloseFigure();
            return p;
        }

        public static LinearGradientBrush Rainbow(RectangleF r)
        {
            var b = new LinearGradientBrush(new RectangleF(r.X - 1, r.Y, r.Width + 2, r.Height), Pink, Violet, 0f);
            b.InterpolationColors = new ColorBlend
            {
                Colors = new[] { Pink, Orange, Cyan, Violet },
                Positions = new[] { 0f, 0.35f, 0.7f, 1f },
            };
            return b;
        }

        // "diskOS Disco" with the rainbow "!" right after it.
        public static void Wordmark(Graphics g, Font f, float x, float y)
        {
            var tf = StringFormat.GenericTypographic;
            using (var t = new SolidBrush(Text)) g.DrawString("diskOS Disco", f, t, x, y, tf);
            float w = g.MeasureString("diskOS Disco", f, 1000, tf).Width + f.Size * 0.08f;
            var bang = g.MeasureString("!", f, 1000, tf);
            using (var b = Rainbow(new RectangleF(x + w, y, Math.Max(4, bang.Width), bang.Height))) g.DrawString("!", f, b, x + w, y, tf);
        }

        // A form in the Disco style, with the header band on top and the body below.
        public static void Style(Form f)
        {
            f.Text = "diskOS Disco! for Windows";
            f.Font = new Font("Segoe UI", 10f);
            f.BackColor = Bg;
            f.ForeColor = Text;
            f.AutoScaleMode = AutoScaleMode.Dpi;
            try { f.Icon = Icon.FromHandle(Disc(28).GetHicon()); } catch { }
        }

        public static TextBox Log()
        {
            return new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, WordWrap = false,
                Font = new Font("Consolas", 9f), BackColor = Bg2, ForeColor = Muted, BorderStyle = BorderStyle.None,
                Margin = new Padding(0, 12, 0, 0)
            };
        }
    }

    // A rounded button: the rainbow for the main action, quiet glass for the rest.
    class PillButton : Button
    {
        public bool Primary;
        bool hover, down;

        public PillButton(string text, bool primary)
        {
            Text = text; Primary = primary;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
            Cursor = Cursors.Hand;
            AutoSize = true;
            Margin = new Padding(8, 4, 0, 0);
            Font = new Font("Segoe UI Semibold", 10f);
        }

        public override Size GetPreferredSize(Size proposed)
        {
            var t = TextRenderer.MeasureText(Text, Font);
            return new Size(t.Width + 40, Math.Max(36, t.Height + 16));
        }

        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnEnabledChanged(EventArgs e) { Cursor = Enabled ? Cursors.Hand : Cursors.Default; Invalidate(); base.OnEnabledChanged(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Bg);
            var r = new RectangleF(1, 1, Width - 3, Height - 3);
            using (var path = Theme.Pill(r))
            {
                Color fore;
                if (!Enabled)
                {
                    using (var b = new SolidBrush(Theme.Card)) g.FillPath(b, path);
                    fore = Theme.Faint;
                }
                else if (Primary)
                {
                    using (var b = Theme.Rainbow(r)) g.FillPath(b, path);
                    if (hover || down) using (var b = new SolidBrush(Color.FromArgb(down ? 50 : 30, 255, 255, 255))) g.FillPath(b, path);
                    fore = Theme.Bg;
                }
                else
                {
                    using (var b = new SolidBrush(down ? Color.FromArgb(44, 44, 60) : hover ? Color.FromArgb(36, 36, 50) : Theme.Card)) g.FillPath(b, path);
                    using (var p = new Pen(hover ? Theme.Faint : Theme.Line)) g.DrawPath(p, path);
                    fore = Theme.Text;
                }
                TextRenderer.DrawText(g, Text, Font, Rectangle.Round(r), fore,
                                      TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            }
            if (Focused && ShowFocusCues)
                using (var p = new Pen(Color.FromArgb(120, Theme.Cyan)) { DashStyle = DashStyle.Dot })
                using (var path = Theme.Pill(new RectangleF(3, 3, Width - 7, Height - 7))) g.DrawPath(p, path);
        }
    }

    // The band across the top: disc, name, the four steps, and About.
    class Header : Control
    {
        static readonly string[] Steps = { "Check", "Prepare", "Install", "Update" };
        readonly int step;
        readonly string title;
        readonly Bitmap disc = Theme.Disc(46);
        readonly LinkLabel about;

        public Header(int step, string title)
        {
            this.step = step; this.title = title;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Dock = DockStyle.Top;
            Height = 150;
            about = new LinkLabel
            {
                Text = "About", AutoSize = true, LinkColor = Theme.Muted, ActiveLinkColor = Theme.Cyan, VisitedLinkColor = Theme.Muted,
                LinkBehavior = LinkBehavior.HoverUnderline, BackColor = Color.Transparent, Font = new Font("Segoe UI", 9.5f),
            };
            about.Click += (s, e) => { using (var f = new AboutForm()) f.ShowDialog(FindForm()); };
            Controls.Add(about);
        }

        protected override void OnLayout(LayoutEventArgs e)
        {
            base.OnLayout(e);
            if (about != null) about.Location = new Point(Width - about.Width - 26, 22);   // (layout runs once before it exists)
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            using (var b = new LinearGradientBrush(ClientRectangle, Color.FromArgb(26, 16, 44), Theme.Bg, 90f)) g.FillRectangle(b, ClientRectangle);
            using (var b = new LinearGradientBrush(new Rectangle(0, 0, Width + 1, 2), Theme.Pink, Theme.Violet, 0f))   // a thin rainbow rim
            {
                b.InterpolationColors = new ColorBlend { Colors = new[] { Theme.Pink, Theme.Orange, Theme.Cyan, Theme.Violet }, Positions = new[] { 0f, .35f, .7f, 1f } };
                g.FillRectangle(b, 0, Height - 2, Width, 2);
            }
            int pad = (disc.Width - 46) / 2;
            g.DrawImage(disc, 22 - pad, 14 - pad);
            using (var f = new Font("Segoe UI", 17f, FontStyle.Bold)) Theme.Wordmark(g, f, 84, 19);
            using (var f = new Font("Segoe UI", 9.5f)) g.DrawString("for Windows", f, new SolidBrush(Theme.Muted), 85, 48);

            // the four steps: dots joined by a line, the current one lit
            float x0 = 30, y = 96, gap = 118;
            using (var p = new Pen(Theme.Line, 2)) g.DrawLine(p, x0, y, x0 + gap * (Steps.Length - 1), y);
            if (step > 1) using (var b = Theme.Rainbow(new RectangleF(x0, y - 1, gap * (step - 1), 2))) g.FillRectangle(b, x0, y - 1, gap * (step - 1), 2);
            for (int i = 0; i < Steps.Length; i++)
            {
                float x = x0 + gap * i;
                bool done = i + 1 < step, now = i + 1 == step;
                float d = now ? 14 : 10;
                if (now)
                    using (var b = new SolidBrush(Color.FromArgb(60, Theme.Cyan))) g.FillEllipse(b, x - d, y - d, d * 2, d * 2);
                using (var b = new SolidBrush(now ? Theme.IriAt(i / 4f + 0.55f) : done ? Theme.Muted : Theme.Line))
                    g.FillEllipse(b, x - d / 2, y - d / 2, d, d);
                using (var f = new Font("Segoe UI", 9f, now ? FontStyle.Bold : FontStyle.Regular))
                {
                    var sz = g.MeasureString(Steps[i], f);
                    g.DrawString(Steps[i], f, new SolidBrush(now ? Theme.Text : Theme.Faint), x - sz.Width / 2, y + 12);
                }
            }
            using (var f = new Font("Segoe UI Semibold", 13f))
            {
                var sz = g.MeasureString(title, f);
                float tx = Math.Max(x0 + gap * (Steps.Length - 1) + 40, Width - sz.Width - 26);
                if (tx + sz.Width < Width - 10) g.DrawString(title, f, new SolidBrush(Theme.Text), tx, y - sz.Height / 2);
            }
        }
    }
}
