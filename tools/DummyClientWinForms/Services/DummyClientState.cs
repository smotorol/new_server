using System;
using System.Collections.Generic;
using DummyClientWinForms.Models;

namespace DummyClientWinForms.Services
{
    public enum ClientStage
    {
        None,
        LoginConnected,
        LoggedIn,
        WorldSelected,
        CharacterSelected,
        WorldConnected,
        EnterWorldPending,
        InWorld,
    }

    public enum DisconnectReason
    {
        None,
        ExplicitLogout,
        ManualReconnect,
        FormClosing,
        NetworkError,
        RemoteClosed,
        ServerClosed,
        WorldTransition,
    }

    public enum ReconnectTarget
    {
        None,
        Login,
        World,
    }

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
        public string ReconnectToken { get; set; } = string.Empty;
        public string WorldHost { get; set; } = string.Empty;
        public int WorldPort { get; set; }
        public bool AutoOverlayZone { get; set; } = true;
        public int ZoneId { get; set; } = 1;
        public int MapId { get; set; } = 1;
        public int CurrentChannelId { get; set; }
        public int CurrentZoneServerId { get; set; }
        public int PosX { get; set; }
        public int PosY { get; set; }
        public bool HasMoveTarget { get; set; }
        public int MoveTargetX { get; set; }
        public int MoveTargetY { get; set; }
        public int LastMoveRequestX { get; set; }
        public int LastMoveRequestY { get; set; }
        public string LastMoveInputSource { get; set; } = "None";
        public string LastMoveBlockedReason { get; set; } = string.Empty;
        public bool LastMoveLocalWalkable { get; set; }
        public bool AwaitingAuthoritativeMoveResult { get; set; }
        public bool WasdActive { get; set; }
        public bool GeometryOverlayEnabled { get; set; } = true;
        public bool PortalMarkersEnabled { get; set; } = true;
        public bool SafeZoneMarkersEnabled { get; set; } = true;
        public bool SpawnMarkersEnabled { get; set; } = true;
        public bool MiniMapEnabled { get; set; } = true;
        public bool MiniMapLegendEnabled { get; set; } = true;
        public bool GeometryOverlayLoaded { get; set; }
        public int GeometryLoadedMapId { get; set; }
        public int GeometrySampleSizeMeters { get; set; } = 5;
        public int MiniMapMapId { get; set; }
        public int MiniMapSampleSizeMeters { get; set; } = 40;
        public int GeometryMismatchCount { get; set; }
        public string LastGeometryCheck { get; set; } = "unknown";
        public int LastGeometryCheckX { get; set; }
        public int LastGeometryCheckY { get; set; }
        public string LastGeometryMismatch { get; set; } = string.Empty;
        public int CoordinateUnitMeters { get; set; } = 1;
        public int AoiRadiusMeters { get; set; } = 50;
        public ushort ZoneMapStateReason { get; set; }
        public uint Hp { get; set; }
        public uint MaxHp { get; set; }
        public uint Atk { get; set; }
        public uint Def { get; set; }
        public uint Gold { get; set; }
        public ClientStage CurrentStage { get; set; } = ClientStage.None;
        public ClientStage LastStableStage { get; set; } = ClientStage.None;
        public DisconnectReason LastDisconnectReason { get; set; } = DisconnectReason.None;
        public DisconnectReason PendingDisconnectReason { get; set; } = DisconnectReason.None;
        public string LastDisconnectDetail { get; set; } = string.Empty;
        public ReconnectTarget LastReconnectTarget { get; set; } = ReconnectTarget.None;
        public ClientStage ReconnectResumeStage { get; set; } = ClientStage.None;
        public bool ReconnectRequested { get; set; }
        public bool ExplicitLogoutRequested { get; set; }
        public bool HardLogoutRequested { get; set; }
        public bool WorldEnterCompleted { get; set; }
        public bool EnterWorldRequested { get; set; }
        public List<WorldSummary> Worlds { get; } = new List<WorldSummary>();
        public List<CharacterSummary> Characters { get; } = new List<CharacterSummary>();
        public Dictionary<string, WorldObject> LiveObjects { get; } = new Dictionary<string, WorldObject>();
        public List<WorldObject> StaticObjects { get; } = new List<WorldObject>();

        public bool CanDirectReconnectToWorld =>
            SelectedCharId != 0 &&
            !string.IsNullOrWhiteSpace(WorldHost) &&
            WorldPort > 0 &&
            (!string.IsNullOrWhiteSpace(ReconnectToken) ||
             (!WorldEnterCompleted && !string.IsNullOrWhiteSpace(WorldToken)));
    }
}
