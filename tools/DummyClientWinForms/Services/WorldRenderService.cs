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

        public Point ScreenToWorld(Point center, Rectangle viewport, Point screenPoint)
        {
            var worldX = center.X + (int)Math.Round((screenPoint.X - viewport.Width / 2f) / Zoom);
            var worldY = center.Y + (int)Math.Round((screenPoint.Y - viewport.Height / 2f) / Zoom);
            return new Point(worldX, worldY);
        }

        public void Render(Graphics g, Rectangle viewport, DummyClientState state, string selectedObjectId, IReadOnlyList<GeometryCellSample> geometrySamples)
        {
            g.Clear(Color.FromArgb(24, 26, 30));
            using (var pen = new Pen(Color.FromArgb(55, 60, 68)))
            {
                for (var x = 0; x < viewport.Width; x += 40) g.DrawLine(pen, x, 0, x, viewport.Height);
                for (var y = 0; y < viewport.Height; y += 40) g.DrawLine(pen, 0, y, viewport.Width, y);
            }

            var center = new Point(state.PosX, state.PosY);
            DrawGeometryOverlay(g, viewport, center, state, geometrySamples);
            DrawAoiCircle(g, viewport, state);
            DrawMoveTarget(g, viewport, center, state);
            foreach (var obj in state.StaticObjects.Concat(state.LiveObjects.Values))
            {
                if (!ShouldRenderObject(state, obj)) {
                    continue;
                }
                DrawObject(g, viewport, center, obj, obj.Id == selectedObjectId);
            }
        }

        public void RenderMiniMap(Graphics g, Rectangle viewport, DummyClientState state, MiniMapSnapshot snapshot, string selectedObjectId)
        {
            g.Clear(Color.FromArgb(18, 20, 24));
            if (!state.MiniMapEnabled || snapshot == null || snapshot.Samples.Count == 0)
            {
                using (var brush = new SolidBrush(Color.FromArgb(210, Color.LightGray)))
                {
                    g.DrawString("MiniMap unavailable", SystemFonts.DefaultFont, brush, 8f, 8f);
                }
                return;
            }

            var bounds = snapshot.Bounds;
            var width = Math.Max(1f, bounds.Width);
            var height = Math.Max(1f, bounds.Height);
            foreach (var sample in snapshot.Samples)
            {
                var rect = WorldRectToMiniMap(bounds, viewport, sample.X, sample.Y, sample.Size, sample.Size);
                using (var brush = new SolidBrush(sample.Walkable
                           ? Color.FromArgb(34, 72, 172, 112)
                           : Color.FromArgb(88, 160, 64, 64)))
                {
                    g.FillRectangle(brush, rect);
                }
            }

            foreach (var obj in state.StaticObjects.Concat(state.LiveObjects.Values))
            {
                if (!ShouldRenderObject(state, obj)) {
                    continue;
                }
                DrawMiniMapObject(g, viewport, bounds, obj, obj.Id == selectedObjectId);
            }

            using (var pen = new Pen(Color.FromArgb(180, Color.DeepSkyBlue), 1.5f))
            {
                var visibleWidth = viewport.Width / Math.Max(0.1f, Zoom);
                var visibleHeight = viewport.Height / Math.Max(0.1f, Zoom);
                var rect = WorldRectToMiniMap(bounds, viewport, state.PosX - visibleWidth / 2f, state.PosY - visibleHeight / 2f, visibleWidth, visibleHeight);
                g.DrawRectangle(pen, rect.X, rect.Y, rect.Width, rect.Height);
            }

            if (state.MiniMapLegendEnabled)
            {
                DrawMiniMapLegend(g, viewport, snapshot, state);
            }
        }

        private static RectangleF WorldRectToMiniMap(RectangleF bounds, Rectangle viewport, float x, float y, float w, float h)
        {
            var scaleX = viewport.Width / Math.Max(1f, bounds.Width);
            var scaleY = viewport.Height / Math.Max(1f, bounds.Height);
            var sx = (x - bounds.Left) * scaleX;
            var sy = (y - bounds.Top) * scaleY;
            return new RectangleF(sx, sy, Math.Max(1f, w * scaleX), Math.Max(1f, h * scaleY));
        }

        private void DrawMiniMapObject(Graphics g, Rectangle viewport, RectangleF bounds, WorldObject obj, bool selected)
        {
            var point = WorldRectToMiniMap(bounds, viewport, obj.X, obj.Y, 1, 1);
            var centerX = point.X;
            var centerY = point.Y;
            var color = Color.White;
            switch (obj.Kind)
            {
                case WorldObjectKind.Player: color = obj is PlayerObject po && po.IsSelf ? Color.DodgerBlue : Color.SkyBlue; break;
                case WorldObjectKind.Monster: color = Color.IndianRed; break;
                case WorldObjectKind.Npc: color = Color.Silver; break;
                case WorldObjectKind.Portal: color = Color.Goldenrod; break;
                case WorldObjectKind.SafeZone: color = Color.MediumSeaGreen; break;
            }

            if (obj.Kind == WorldObjectKind.SafeZone && obj.Radius > 0)
            {
                var radiusRect = WorldRectToMiniMap(bounds, viewport, obj.X - obj.Radius, obj.Y - obj.Radius, obj.Radius * 2f, obj.Radius * 2f);
                using (var fill = new SolidBrush(Color.FromArgb(24, color)))
                using (var pen = new Pen(Color.FromArgb(100, color), 1f))
                {
                    g.FillEllipse(fill, radiusRect);
                    g.DrawEllipse(pen, radiusRect);
                }
                return;
            }

            using (var brush = new SolidBrush(color))
            {
                var size = obj.Kind == WorldObjectKind.Player ? 5f : 4f;
                g.FillEllipse(brush, centerX - size / 2f, centerY - size / 2f, size, size);
            }

            if (selected)
            {
                using (var pen = new Pen(Color.Yellow, 1.5f))
                {
                    g.DrawEllipse(pen, centerX - 5f, centerY - 5f, 10f, 10f);
                }
            }
        }

        private void DrawMiniMapLegend(Graphics g, Rectangle viewport, MiniMapSnapshot snapshot, DummyClientState state)
        {
            var legendRect = new RectangleF(8f, 8f, Math.Min(190f, viewport.Width - 16f), 74f);
            using (var fill = new SolidBrush(Color.FromArgb(170, 20, 20, 24)))
            using (var border = new Pen(Color.FromArgb(120, Color.DimGray), 1f))
            using (var textBrush = new SolidBrush(Color.Gainsboro))
            {
                g.FillRectangle(fill, legendRect);
                g.DrawRectangle(border, legendRect.X, legendRect.Y, legendRect.Width, legendRect.Height);
                g.DrawString($"Map {snapshot.MapId} / sample {snapshot.SampleSizeMeters}m", SystemFonts.DefaultFont, textBrush, legendRect.X + 8f, legendRect.Y + 6f);
                g.DrawString("Blue=self  Gold=portal  Green=safe  Red=spawn  Gray=npc", SystemFonts.DefaultFont, textBrush, legendRect.X + 8f, legendRect.Y + 26f);
                g.DrawString($"Pos=({state.PosX},{state.PosY})", SystemFonts.DefaultFont, textBrush, legendRect.X + 8f, legendRect.Y + 46f);
            }
        }

        private bool ShouldRenderObject(DummyClientState state, WorldObject obj)
        {
            if (!obj.IsStaticOverlay)
            {
                return true;
            }

            switch (obj.Kind)
            {
                case WorldObjectKind.Portal:
                    return state.PortalMarkersEnabled;
                case WorldObjectKind.SafeZone:
                    return state.SafeZoneMarkersEnabled;
                case WorldObjectKind.Monster:
                case WorldObjectKind.Npc:
                    return state.SpawnMarkersEnabled;
                default:
                    return true;
            }
        }

        private void DrawGeometryOverlay(Graphics g, Rectangle viewport, Point center, DummyClientState state, IReadOnlyList<GeometryCellSample> geometrySamples)
        {
            if (!state.GeometryOverlayEnabled || geometrySamples == null || geometrySamples.Count == 0)
            {
                return;
            }

            foreach (var sample in geometrySamples)
            {
                var topLeft = WorldToScreen(center, viewport, new Point(sample.X, sample.Y));
                var sizePx = Math.Max(2f, sample.Size * Zoom);
                var rect = new RectangleF(topLeft.X, topLeft.Y, sizePx, sizePx);
                using (var brush = new SolidBrush(sample.Walkable
                           ? Color.FromArgb(32, 56, 168, 108)
                           : Color.FromArgb(76, 168, 64, 64)))
                {
                    g.FillRectangle(brush, rect);
                }

                if (!sample.Walkable)
                {
                    using (var pen = new Pen(Color.FromArgb(96, 120, 36, 36), 1f))
                    {
                        g.DrawLine(pen, rect.Left, rect.Top, rect.Right, rect.Bottom);
                        g.DrawLine(pen, rect.Right, rect.Top, rect.Left, rect.Bottom);
                    }
                }
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

        private void DrawMoveTarget(Graphics g, Rectangle viewport, Point center, DummyClientState state)
        {
            if (!state.HasMoveTarget)
            {
                return;
            }

            var p = WorldToScreen(center, viewport, new Point(state.MoveTargetX, state.MoveTargetY));
            using (var pen = new Pen(Color.LimeGreen, 2f))
            {
                g.DrawEllipse(pen, p.X - 8f, p.Y - 8f, 16f, 16f);
                g.DrawLine(pen, p.X - 10f, p.Y, p.X + 10f, p.Y);
                g.DrawLine(pen, p.X, p.Y - 10f, p.X, p.Y + 10f);
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
                case WorldObjectKind.SafeZone: brush = Brushes.MediumSeaGreen; break;
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
            else if (obj.Kind == WorldObjectKind.SafeZone)
            {
                var r = Math.Max(4f, obj.Radius * Zoom);
                using (var fill = new SolidBrush(Color.FromArgb(24, Color.MediumSeaGreen)))
                using (var pen = new Pen(Color.FromArgb(120, Color.MediumSeaGreen), 2f))
                {
                    g.FillEllipse(fill, p.X - r, p.Y - r, r * 2, r * 2);
                    g.DrawEllipse(pen, p.X - r, p.Y - r, r * 2, r * 2);
                }
            }
            else
            {
                g.FillRectangle(brush, rect);
            }
            if (obj.Kind != WorldObjectKind.SafeZone && obj.Radius > 0)
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
