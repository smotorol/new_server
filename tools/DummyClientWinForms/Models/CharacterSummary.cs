namespace DummyClientWinForms.Models
{
    public sealed class CharacterSummary
    {
        public ulong CharId { get; set; }
        public string Name { get; set; } = string.Empty;
        public uint Level { get; set; }
        public ushort Job { get; set; }
        public uint AppearanceCode { get; set; }
        public ulong LastLoginAtEpochSec { get; set; }
        public override string ToString() => $"{Name} (Lv.{Level}, Job {Job}, {CharId})";
    }
}
