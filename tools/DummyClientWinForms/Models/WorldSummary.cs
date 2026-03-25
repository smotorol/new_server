namespace DummyClientWinForms.Models
{
    public sealed class WorldSummary
    {
        public ushort WorldId { get; set; }
        public ushort ChannelId { get; set; }
        public ushort ActiveZoneCount { get; set; }
        public ushort LoadScore { get; set; }
        public ushort PublicPort { get; set; }
        public uint Flags { get; set; }
        public string ServerName { get; set; } = string.Empty;
        public string PublicHost { get; set; } = string.Empty;
        public override string ToString() => $"W{WorldId}-C{ChannelId} {ServerName} {PublicHost}:{PublicPort} load={LoadScore} zones={ActiveZoneCount}";
    }
}
