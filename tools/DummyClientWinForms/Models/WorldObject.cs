using System.Drawing;

namespace DummyClientWinForms.Models
{
    public enum WorldObjectKind
    {
        Player,
        Monster,
        Npc,
        Portal,
    }

    public class WorldObject
    {
        public string Id { get; set; } = string.Empty;
        public WorldObjectKind Kind { get; set; }
        public string Label { get; set; } = string.Empty;
        public int ZoneId { get; set; }
        public int MapId { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public int Radius { get; set; }
        public int Heading { get; set; }
        public bool IsStaticOverlay { get; set; }
        public bool IsSelected { get; set; }
        public Point ToPoint() => new Point(X, Y);
    }
}
