using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;

namespace DummyClientWinForms.Services
{
    public sealed class WmGeometryLoader
    {
        private readonly string _mapsRoot;

        public WmGeometryLoader(string mapsRoot = null)
        {
            _mapsRoot = !string.IsNullOrWhiteSpace(mapsRoot)
                ? mapsRoot
                : (Environment.GetEnvironmentVariable("DC_MAP_RESOURCES_PATH")
                    ?? Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "..", "resources", "maps"));
            _mapsRoot = Path.GetFullPath(_mapsRoot);
        }

        public string MapsRoot => _mapsRoot;

        public MapGeometryData Load(int mapId)
        {
            var mapDir = Path.Combine(_mapsRoot, $"map_{mapId:000}");
            var wmPath = Path.Combine(mapDir, "base.wm");
            if (!File.Exists(wmPath))
            {
                throw new FileNotFoundException("WM file not found", wmPath);
            }

            var fileBytes = File.ReadAllBytes(wmPath);
            if (fileBytes.Length < 8)
            {
                throw new InvalidDataException("WM header is too small");
            }

            var originalSize = BitConverter.ToInt32(fileBytes, 0);
            var compressedSize = BitConverter.ToInt32(fileBytes, 4);
            if (originalSize <= 0 || compressedSize <= 0 || fileBytes.Length < 8 + compressedSize)
            {
                throw new InvalidDataException("WM header size values are invalid");
            }

            var compressedPayload = new byte[compressedSize];
            Buffer.BlockCopy(fileBytes, 8, compressedPayload, 0, compressedSize);
            var original = DecompressZlib(compressedPayload, originalSize);
            using (var ms = new MemoryStream(original, writable: false))
            using (var br = new BinaryReader(ms))
            {
                var triangleCount = br.ReadInt32();
                if (triangleCount <= 0)
                {
                    throw new InvalidDataException("WM triangle count is invalid");
                }

                var triangles = new Triangle2D[triangleCount];
                for (var i = 0; i < triangleCount; i++)
                {
                    br.ReadInt32();
                    var ax = br.ReadSingle();
                    br.ReadSingle();
                    var az = br.ReadSingle();
                    SkipVertexPayload(br);
                    var bx = br.ReadSingle();
                    br.ReadSingle();
                    var bz = br.ReadSingle();
                    SkipVertexPayload(br);
                    var cx = br.ReadSingle();
                    br.ReadSingle();
                    var cz = br.ReadSingle();
                    SkipVertexPayload(br);
                    br.ReadBytes(32);
                    triangles[i] = new Triangle2D(ax, az, bx, bz, cx, cz);
                }

                var nodeCount = br.ReadInt32();
                br.ReadInt32();
                if (nodeCount <= 0)
                {
                    throw new InvalidDataException("WM quadtree node count is invalid");
                }

                var nodes = new QuadNode[nodeCount];
                for (var i = 0; i < nodeCount; i++)
                {
                    var minX = br.ReadSingle();
                    br.ReadSingle();
                    var minZ = br.ReadSingle();
                    var maxX = br.ReadSingle();
                    br.ReadSingle();
                    var maxZ = br.ReadSingle();
                    var trisNum = br.ReadInt32();
                    var hasTris = br.ReadInt32() != 0;
                    int[] trisIndex = Array.Empty<int>();
                    if (hasTris)
                    {
                        trisIndex = new int[trisNum];
                        for (var t = 0; t < trisNum; t++)
                        {
                            trisIndex[t] = br.ReadInt32();
                        }
                    }

                    var childIndices = new int[4];
                    for (var c = 0; c < 4; c++)
                    {
                        childIndices[c] = br.ReadInt32();
                    }

                    nodes[i] = new QuadNode(minX, minZ, maxX, maxZ, trisIndex, childIndices);
                }

                return new MapGeometryData(mapId, wmPath, triangles, nodes);
            }
        }

        private static void SkipVertexPayload(BinaryReader br)
        {
            br.ReadBytes((3 + 2 + 2) * sizeof(float));
        }

        private static byte[] DecompressZlib(byte[] payload, int originalSize)
        {
            if (payload.Length < 6)
            {
                throw new InvalidDataException("Compressed WM payload is too small");
            }

            var rawDeflate = new byte[payload.Length - 6];
            Buffer.BlockCopy(payload, 2, rawDeflate, 0, rawDeflate.Length);
            using (var input = new MemoryStream(rawDeflate, writable: false))
            using (var deflate = new DeflateStream(input, CompressionMode.Decompress))
            using (var output = new MemoryStream(originalSize))
            {
                deflate.CopyTo(output);
                var result = output.ToArray();
                if (result.Length != originalSize)
                {
                    throw new InvalidDataException($"WM decompressed size mismatch. expected={originalSize} actual={result.Length}");
                }
                return result;
            }
        }
    }

    public sealed class MapGeometryData
    {
        private readonly Triangle2D[] _triangles;
        private readonly QuadNode[] _nodes;

        internal MapGeometryData(int mapId, string wmPath, Triangle2D[] triangles, QuadNode[] nodes)
        {
            MapId = mapId;
            WmPath = wmPath;
            _triangles = triangles;
            _nodes = nodes;
            if (nodes.Length > 0)
            {
                MinX = nodes[0].MinX;
                MinY = nodes[0].MinY;
                MaxX = nodes[0].MaxX;
                MaxY = nodes[0].MaxY;
            }
        }

        public int MapId { get; }
        public string WmPath { get; }
        public float MinX { get; }
        public float MinY { get; }
        public float MaxX { get; }
        public float MaxY { get; }

        public bool IsWalkable(float x, float y)
        {
            if (_nodes.Length == 0)
            {
                return false;
            }
            if (x < MinX || x > MaxX || y < MinY || y > MaxY)
            {
                return false;
            }

            var nodeIndex = 0;
            while (true)
            {
                var node = _nodes[nodeIndex];
                if (!node.HasChildren)
                {
                    break;
                }

                var matchedChild = -1;
                for (var i = 0; i < node.ChildIndices.Length; i++)
                {
                    var childIndex = node.ChildIndices[i];
                    if (childIndex < 0 || childIndex >= _nodes.Length)
                    {
                        continue;
                    }
                    if (_nodes[childIndex].Contains(x, y))
                    {
                        matchedChild = childIndex;
                        break;
                    }
                }

                if (matchedChild < 0)
                {
                    return false;
                }
                nodeIndex = matchedChild;
            }

            var leaf = _nodes[nodeIndex];
            for (var i = 0; i < leaf.TriangleIndices.Length; i++)
            {
                var triIndex = leaf.TriangleIndices[i];
                if (triIndex < 0 || triIndex >= _triangles.Length)
                {
                    continue;
                }
                if (_triangles[triIndex].Contains(x, y))
                {
                    return true;
                }
            }

            return false;
        }
    }

    public sealed class GeometryCellSample
    {
        public int X { get; set; }
        public int Y { get; set; }
        public int Size { get; set; }
        public bool Walkable { get; set; }
    }

    internal struct Triangle2D
    {
        public Triangle2D(float ax, float ay, float bx, float by, float cx, float cy)
        {
            Ax = ax;
            Ay = ay;
            Bx = bx;
            By = by;
            Cx = cx;
            Cy = cy;
        }

        public float Ax { get; }
        public float Ay { get; }
        public float Bx { get; }
        public float By { get; }
        public float Cx { get; }
        public float Cy { get; }

        public bool Contains(float px, float py)
        {
            var v0x = Bx - Ax;
            var v0y = By - Ay;
            var v1x = Cx - Ax;
            var v1y = Cy - Ay;
            var v2x = px - Ax;
            var v2y = py - Ay;
            var dot00 = v0x * v0x + v0y * v0y;
            var dot01 = v0x * v1x + v0y * v1y;
            var dot02 = v0x * v2x + v0y * v2y;
            var dot11 = v1x * v1x + v1y * v1y;
            var dot12 = v1x * v2x + v1y * v2y;
            var denom = dot00 * dot11 - dot01 * dot01;
            if (Math.Abs(denom) < 1e-6f)
            {
                return false;
            }

            var invDenom = 1.0f / denom;
            var u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            var v = (dot00 * dot12 - dot01 * dot02) * invDenom;
            return u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f;
        }
    }

    internal sealed class QuadNode
    {
        public QuadNode(float minX, float minY, float maxX, float maxY, int[] triangleIndices, int[] childIndices)
        {
            MinX = minX;
            MinY = minY;
            MaxX = maxX;
            MaxY = maxY;
            TriangleIndices = triangleIndices ?? Array.Empty<int>();
            ChildIndices = childIndices ?? Array.Empty<int>();
        }

        public float MinX { get; }
        public float MinY { get; }
        public float MaxX { get; }
        public float MaxY { get; }
        public int[] TriangleIndices { get; }
        public int[] ChildIndices { get; }
        public bool HasChildren => ChildIndices.Length == 4 && (ChildIndices[0] != -1 || ChildIndices[1] != -1 || ChildIndices[2] != -1 || ChildIndices[3] != -1);

        public bool Contains(float x, float y)
        {
            return x >= MinX && x <= MaxX && y >= MinY && y <= MaxY;
        }
    }
}


