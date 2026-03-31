using System;
using System.Collections.Generic;
using UnityEngine;

namespace DummyClientUnity.Runtime
{
    [Serializable]
    public class DummyClientState
    {
        public string Host = "127.0.0.1";
        public int Port = 26788;
        public string LoginId = "test";
        public string Password = "test";
        public ulong AccountId;
        public ulong CharId;
        public ushort SelectedWorldId;
        public string SelectedServerCode = string.Empty;
        public string SelectedCharacterName = string.Empty;
        public uint ZoneId;
        public uint MapId;
        public int X;
        public int Y;
        public string LoginSession = string.Empty;
        public string WorldToken = string.Empty;
        public string LastReason = string.Empty;
        public bool Connected;
        public bool LoggedIn;
        public bool WorldSelected;
        public bool CharacterSelected;
        public bool InWorld;
        public bool ReconnectRequested;
        public List<string> WorldEntries = new List<string>();
        public List<string> Characters = new List<string>();
    }
}
