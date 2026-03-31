namespace DummyClientWinForms.Models
{
    public sealed class PlayerObject : WorldObject
    {
        public ulong CharId { get; set; }
        public bool IsSelf { get; set; }
    }
}
