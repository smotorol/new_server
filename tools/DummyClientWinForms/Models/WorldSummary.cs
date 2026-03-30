namespace DummyClientWinForms.Models
{
    public sealed class WorldSummary
    {
        public ushort WorldId { get; set; }
        public string ServerCode { get; set; } = string.Empty;
        public string DisplayName { get; set; } = string.Empty;
        public string Region { get; set; } = string.Empty;
        public string Status { get; set; } = string.Empty;
        public bool Recommended { get; set; }
        public uint Population { get; set; }
        public uint Capacity { get; set; }
        public ushort ChannelId { get; set; }
        public ushort ActiveZoneCount { get; set; }
        public ushort LoadScore { get; set; }
        public ushort PublicPort { get; set; }
        public uint Flags { get; set; }
        public string ServerName { get; set; } = string.Empty;
        public string PublicHost { get; set; } = string.Empty;

        public override string ToString()
        {
            var title = string.IsNullOrWhiteSpace(DisplayName) ? ServerName : DisplayName;
            var code = string.IsNullOrWhiteSpace(ServerCode) ? $"W{WorldId}" : ServerCode;
            return $"{code} {title} {PublicHost}:{PublicPort} status={Status} pop={Population}/{Capacity}";
        }
    }
}
