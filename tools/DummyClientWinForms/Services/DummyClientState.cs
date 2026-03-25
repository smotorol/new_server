using System.Collections.Generic;
using DummyClientWinForms.Models;

namespace DummyClientWinForms.Services
{
    public sealed class DummyClientState
    {
        public string LoginHost { get; set; } = "127.0.0.1";
        public int LoginPort { get; set; } = 26788;
        public string LoginId { get; set; } = "test1";
        public string Password { get; set; } = "pw1";
        public bool ConnectedToLogin { get; set; }
        public bool ConnectedToWorld { get; set; }
        public bool LoggedIn { get; set; }
        public bool InWorld { get; set; }
        public ulong AccountId { get; set; }
        public ulong SelectedCharId { get; set; }
        public ushort SelectedWorldId { get; set; }
        public ushort SelectedChannelId { get; set; }
        public string LoginSession { get; set; } = string.Empty;
        public string WorldToken { get; set; } = string.Empty;
        public string WorldHost { get; set; } = string.Empty;
        public int WorldPort { get; set; }
        public bool AutoOverlayZone { get; set; } = true;
        public int ZoneId { get; set; } = 1;
        public int MapId { get; set; } = 1;
        public int PosX { get; set; }
        public int PosY { get; set; }
        public ushort ZoneMapStateReason { get; set; }
        public uint Hp { get; set; }
        public uint MaxHp { get; set; }
        public uint Atk { get; set; }
        public uint Def { get; set; }
        public uint Gold { get; set; }
        public List<WorldSummary> Worlds { get; } = new List<WorldSummary>();
        public List<CharacterSummary> Characters { get; } = new List<CharacterSummary>();
        public Dictionary<string, WorldObject> LiveObjects { get; } = new Dictionary<string, WorldObject>();
        public List<WorldObject> StaticObjects { get; } = new List<WorldObject>();
    }
}
