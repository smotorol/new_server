using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using DummyClientWinForms.Models;

namespace DummyClientWinForms.Services
{
    public sealed class WorldRenderService
    {
        // 1 world unit == 1 meter. Keep roughly 100-150m visible in the default viewport.
        public float Zoom { get; set; } = 4.0f;

        public PointF WorldToScreen(Point center, Rectangle viewport, Point objectPos)
        {
            var dx = (objectPos.X - center.X) * Zoom;
            var dy = (objectPos.Y - center.Y) * Zoom;
            return new PointF(viewport.Width / 2f + dx, viewport.Height / 2f + dy);
        }

        public void Render(Graphics g, Rectangle viewport, DummyClientState state, string selectedObjectId)
        {
            g.Clear(Color.FromArgb(24, 26, 30));
            using (var pen = new Pen(Color.FromArgb(55, 60, 68)))
            {
                for (var x = 0; x < viewport.Width; x += 40) g.DrawLine(pen, x, 0, x, viewport.Height);
                for (var y = 0; y < viewport.Height; y += 40) g.DrawLine(pen, 0, y, viewport.Width, y);
            }

            var center = new Point(state.PosX, state.PosY);
            DrawAoiCircle(g, viewport, state);
            foreach (var obj in state.StaticObjects.Concat(state.LiveObjects.Values))
            {
                DrawObject(g, viewport, center, obj, obj.Id == selectedObjectId);
            }
        }

        private void DrawAoiCircle(Graphics g, Rectangle viewport, DummyClientState state)
        {
            if (state.AoiRadiusMeters <= 0)
            {
                return;
            }

            var center = new PointF(viewport.Width / 2f, viewport.Height / 2f);
            var radius = Math.Max(8f, state.AoiRadiusMeters * Zoom);
            using (var pen = new Pen(Color.FromArgb(96, Color.DeepSkyBlue), 2f))
            {
                g.DrawEllipse(pen, center.X - radius, center.Y - radius, radius * 2, radius * 2);
            }
        }

        private void DrawObject(Graphics g, Rectangle viewport, Point center, WorldObject obj, bool selected)
        {
            var p = WorldToScreen(center, viewport, obj.ToPoint());
            var rect = new RectangleF(p.X - 6, p.Y - 6, 12, 12);
            var brush = Brushes.White;
            switch (obj.Kind)
            {
                case WorldObjectKind.Player: brush = obj is PlayerObject po && po.IsSelf ? Brushes.DodgerBlue : Brushes.SkyBlue; break;
                case WorldObjectKind.Monster: brush = Brushes.IndianRed; break;
                case WorldObjectKind.Npc: brush = Brushes.Gray; break;
                case WorldObjectKind.Portal: brush = Brushes.Goldenrod; break;
            }
            if (obj.Kind == WorldObjectKind.Monster)
            {
                var points = new[] { new PointF(p.X, p.Y - 7), new PointF(p.X - 7, p.Y + 6), new PointF(p.X + 7, p.Y + 6) };
                g.FillPolygon(brush, points);
            }
            else if (obj.Kind == WorldObjectKind.Npc || obj.Kind == WorldObjectKind.Portal)
            {
                g.FillEllipse(brush, rect);
            }
            else
            {
                g.FillRectangle(brush, rect);
            }
            if (obj.Radius > 0)
            {
                var r = Math.Max(4f, obj.Radius * Zoom);
                using (var pen = new Pen(Color.FromArgb(70, Color.Gold)))
                {
                    g.DrawEllipse(pen, p.X - r, p.Y - r, r * 2, r * 2);
                }
            }
            if (selected)
            {
                using (var pen = new Pen(Color.Yellow, 2f))
                {
                    g.DrawRectangle(pen, rect.X - 3, rect.Y - 3, rect.Width + 6, rect.Height + 6);
                }
            }
        }
    }
}
