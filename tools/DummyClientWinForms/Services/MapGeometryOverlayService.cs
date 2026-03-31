using System;
using System.Collections.Generic;
using System.Drawing;

namespace DummyClientWinForms.Services
{
    public sealed class MapGeometryOverlayService
    {
        private const int DefaultSampleSizeMeters = 5;
        private const int MinSampleSizeMeters = 2;
        private const int MaxSampleSizeMeters = 12;
        private const int MiniMapSampleSizeMeters = 40;

        private readonly WmGeometryLoader _loader;
        private readonly Dictionary<int, MapGeometryData> _cache = new Dictionary<int, MapGeometryData>();
        private readonly Dictionary<string, MiniMapSnapshot> _miniMapCache = new Dictionary<string, MiniMapSnapshot>();

        public MapGeometryOverlayService(WmGeometryLoader loader = null)
        {
            _loader = loader ?? new WmGeometryLoader();
        }

        public string MapsRoot => _loader.MapsRoot;
        public int SampleSizeMeters { get; set; } = DefaultSampleSizeMeters;

        public bool TryEnsureLoaded(int mapId, out MapGeometryData geometry, out string reason)
        {
            if (mapId <= 0)
            {
                geometry = null;
                reason = "invalid_map_id";
                return false;
            }
            if (_cache.TryGetValue(mapId, out geometry))
            {
                reason = string.Empty;
                return true;
            }

            try
            {
                geometry = _loader.Load(mapId);
                _cache[mapId] = geometry;
                reason = string.Empty;
                return true;
            }
            catch (Exception ex)
            {
                geometry = null;
                reason = ex.Message;
                return false;
            }
        }

        public int ResolveSampleSizeMeters(float zoom)
        {
            if (zoom >= 8f) return 2;
            if (zoom >= 5f) return 3;
            if (zoom >= 3f) return 5;
            if (zoom >= 1.5f) return 8;
            return 12;
        }

        public bool TryCheckPoint(int mapId, int x, int y, out bool walkable, out string reason)
        {
            MapGeometryData geometry;
            if (!TryEnsureLoaded(mapId, out geometry, out reason))
            {
                walkable = false;
                return false;
            }

            walkable = geometry.IsWalkable(x, y);
            reason = walkable ? "walkable" : "blocked";
            return true;
        }

        public IReadOnlyList<GeometryCellSample> BuildViewportSamples(int mapId, Point center, Rectangle viewport, float zoom, out int sampleSizeMeters)
        {
            MapGeometryData geometry;
            string reason;
            if (!TryEnsureLoaded(mapId, out geometry, out reason) || zoom <= 0.01f)
            {
                sampleSizeMeters = SampleSizeMeters;
                return Array.Empty<GeometryCellSample>();
            }

            sampleSizeMeters = Math.Max(MinSampleSizeMeters, Math.Min(MaxSampleSizeMeters, ResolveSampleSizeMeters(zoom)));
            return BuildSamplesForBounds(geometry, center.X - viewport.Width / (2f * zoom), center.Y - viewport.Height / (2f * zoom), center.X + viewport.Width / (2f * zoom), center.Y + viewport.Height / (2f * zoom), sampleSizeMeters);
        }

        public bool TryGetMiniMapSnapshot(int mapId, out MiniMapSnapshot snapshot, out string reason)
        {
            MapGeometryData geometry;
            if (!TryEnsureLoaded(mapId, out geometry, out reason))
            {
                snapshot = null;
                return false;
            }

            var key = $"{mapId}:{MiniMapSampleSizeMeters}";
            if (_miniMapCache.TryGetValue(key, out snapshot))
            {
                reason = string.Empty;
                return true;
            }

            var bounds = RectangleF.FromLTRB(geometry.MinX, geometry.MinY, geometry.MaxX, geometry.MaxY);
            var samples = BuildSamplesForBounds(geometry, geometry.MinX, geometry.MinY, geometry.MaxX, geometry.MaxY, MiniMapSampleSizeMeters);
            snapshot = new MiniMapSnapshot(mapId, bounds, samples, MiniMapSampleSizeMeters);
            _miniMapCache[key] = snapshot;
            reason = string.Empty;
            return true;
        }

        private static IReadOnlyList<GeometryCellSample> BuildSamplesForBounds(MapGeometryData geometry, float minX, float minY, float maxX, float maxY, int sampleSize)
        {
            var startX = (int)Math.Floor(minX / sampleSize) * sampleSize;
            var endX = (int)Math.Ceiling(maxX / sampleSize) * sampleSize;
            var startY = (int)Math.Floor(minY / sampleSize) * sampleSize;
            var endY = (int)Math.Ceiling(maxY / sampleSize) * sampleSize;

            var samples = new List<GeometryCellSample>();
            for (var y = startY; y <= endY; y += sampleSize)
            {
                for (var x = startX; x <= endX; x += sampleSize)
                {
                    var sampleCenterX = x + sampleSize / 2;
                    var sampleCenterY = y + sampleSize / 2;
                    var walkable = geometry.IsWalkable(sampleCenterX, sampleCenterY);
                    samples.Add(new GeometryCellSample
                    {
                        X = x,
                        Y = y,
                        Size = sampleSize,
                        Walkable = walkable,
                    });
                }
            }
            return samples;
        }
    }

    public sealed class MiniMapSnapshot
    {
        public MiniMapSnapshot(int mapId, RectangleF bounds, IReadOnlyList<GeometryCellSample> samples, int sampleSizeMeters)
        {
            MapId = mapId;
            Bounds = bounds;
            Samples = samples ?? Array.Empty<GeometryCellSample>();
            SampleSizeMeters = sampleSizeMeters;
        }

        public int MapId { get; }
        public RectangleF Bounds { get; }
        public IReadOnlyList<GeometryCellSample> Samples { get; }
        public int SampleSizeMeters { get; }
    }
}
