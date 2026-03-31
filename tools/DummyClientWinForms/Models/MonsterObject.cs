namespace DummyClientWinForms.Models
{
    public sealed class MonsterObject : WorldObject
    {
        public int MonsterTemplateMin { get; set; }
        public int MonsterTemplateMax { get; set; }
        public int SpawnCount { get; set; }
    }
}
