using System;
using System.Collections.Generic;
using DummyClientWinForms.Models;
using Google.Protobuf;
using LoginProto = Dc.Proto.Client.Login;
using CommonProto = Dc.Proto.Common;
using WorldProto = Dc.Proto.Client.World;

namespace DummyClientWinForms.Network
{
    public static class ClientProtocol
    {
        public const int LoginIdLen = 33;
        public const int PasswordLen = 65;
        public const int LoginSessionLen = 65;
        public const int WorldTokenLen = 33;
        public const int WorldHostLen = 65;
        public const int CharNameLen = 21;
        public const int FailReasonLen = 65;
        public const int ServiceNameLen = 33;

        public const ushort LoginRequest = 1001;
        public const ushort WorldListRequest = 1002;
        public const ushort WorldSelectRequest = 1003;
        public const ushort CharacterListRequest = 1004;
        public const ushort CharacterSelectRequest = 1005;
        public const ushort LoginResult = 1101;
        public const ushort WorldListResponse = 1102;
        public const ushort WorldSelectResponse = 1103;
        public const ushort CharacterListResponse = 1104;
        public const ushort CharacterSelectResponse = 1105;
        public const ushort EnterWorldWithToken = 2001;
        public const ushort EnterWorldResult = 2101;

        public const ushort AddGold = 2;
        public const ushort GetStats = 10;
        public const ushort ZoneMapState = 12;
        public const ushort HealSelf = 11;
        public const ushort SpawnMonster = 20;
        public const ushort AttackMonster = 21;
        public const ushort Move = 40;
        public const ushort Stats = 10;
        public const ushort SpawnMonsterOk = 20;
        public const ushort AttackResult = 21;
        public const ushort PlayerSpawn = 40;
        public const ushort PlayerDespawn = 41;
        public const ushort PlayerMove = 42;
        public const ushort PlayerMoveBatch = 43;
        public const ushort PlayerSpawnBatch = 44;
        public const ushort PlayerDespawnBatch = 45;

        public static byte[] BuildLoginRequest(string loginId, string password)
        {
            return new LoginProto.LoginRequest
            {
                LoginId = loginId ?? string.Empty,
                Password = password ?? string.Empty,
                ClientVersion = "dummy-winforms-first-path"
            }.ToByteArray();
        }

        public static byte[] BuildWorldListRequest()
        {
            return new LoginProto.WorldListRequest().ToByteArray();
        }

        public static byte[] BuildWorldSelectRequest(ushort worldId, string serverCode)
        {
            return new LoginProto.WorldSelectRequest
            {
                WorldId = worldId,
                ServerCode = serverCode ?? string.Empty
            }.ToByteArray();
        }

        public static byte[] BuildCharacterListRequest()
        {
            return new LoginProto.CharacterListRequest().ToByteArray();
        }

        public static byte[] BuildCharacterSelectRequest(ulong charId)
        {
            return new LoginProto.CharacterSelectRequest
            {
                CharId = charId
            }.ToByteArray();
        }

        public static byte[] BuildEnterWorldRequest(ulong accountId, string loginSession, string worldToken)
        {
            return new WorldProto.EnterWorldWithTokenRequest
            {
                AccountId = accountId,
                LoginSession = loginSession ?? string.Empty,
                WorldToken = worldToken ?? string.Empty
            }.ToByteArray();
        }

        public static byte[] BuildGetStats()
        {
            return new WorldProto.GetStatsRequest().ToByteArray();
        }

        public static byte[] BuildMove(int x, int y)
        {
            return new WorldProto.MoveRequest
            {
                X = x,
                Y = y
            }.ToByteArray();
        }

        public static byte[] BuildHeal(uint amount) { using (var w = new PacketWriter()) { w.WriteUInt32(amount); return w.ToArray(); } }
        public static byte[] BuildGold(uint amount) { using (var w = new PacketWriter()) { w.WriteUInt32(amount); return w.ToArray(); } }
        public static byte[] BuildSpawnMonster(uint templateId) { using (var w = new PacketWriter()) { w.WriteUInt32(templateId); return w.ToArray(); } }
        public static byte[] BuildAttackMonster(ulong monsterId) { using (var w = new PacketWriter()) { w.WriteUInt64(monsterId); return w.ToArray(); } }

        public static LoginResultModel ParseLoginResult(byte[] body)
        {
            var msg = LoginProto.LoginResult.Parser.ParseFrom(body);
            return new LoginResultModel
            {
                Ok = msg.Ok,
                AccountId = msg.AccountId,
                LoginSession = msg.LoginSession ?? string.Empty,
                FailReason = msg.FailReason ?? string.Empty,
                WorldEntries = msg.WorldEntries.Count
            };
        }

        public static List<WorldSummary> ParseWorldList(byte[] body, out bool ok, out string failReason)
        {
            var msg = LoginProto.WorldListResponse.Parser.ParseFrom(body);
            ok = msg.Ok;
            failReason = msg.FailReason ?? string.Empty;
            var list = new List<WorldSummary>();
            foreach (var entry in msg.WorldEntries)
            {
                var endpoint = entry.Endpoints.Count > 0 ? entry.Endpoints[0] : null;
                list.Add(new WorldSummary
                {
                    WorldId = (ushort)entry.WorldId,
                    ServerCode = entry.ServerCode ?? string.Empty,
                    DisplayName = entry.DisplayName ?? string.Empty,
                    Region = entry.Region ?? string.Empty,
                    Status = entry.Status.ToString(),
                    Recommended = entry.Recommended,
                    Population = entry.Population,
                    Capacity = entry.Capacity,
                    PublicHost = endpoint?.Host ?? string.Empty,
                    PublicPort = endpoint != null ? (ushort)endpoint.Port : (ushort)0,
                    ServerName = string.IsNullOrWhiteSpace(entry.DisplayName) ? entry.ServerCode : entry.DisplayName,
                    ActiveZoneCount = (ushort)Math.Min(entry.Population, ushort.MaxValue),
                    LoadScore = 0,
                    Flags = 0,
                    ChannelId = 0,
                });
            }
            return list;
        }

        public static WorldSelectResultModel ParseWorldSelect(byte[] body)
        {
            var msg = LoginProto.WorldSelectResponse.Parser.ParseFrom(body);
            var endpoint = msg.SelectedEntry != null && msg.SelectedEntry.Endpoints.Count > 0 ? msg.SelectedEntry.Endpoints[0] : null;
            return new WorldSelectResultModel
            {
                Ok = msg.Ok,
                FailReason = msg.FailReason ?? string.Empty,
                WorldId = (ushort)msg.WorldId,
                ServerCode = msg.ServerCode ?? string.Empty,
                WorldPort = endpoint != null ? (ushort)endpoint.Port : (ushort)0,
                WorldHost = endpoint?.Host ?? string.Empty,
            };
        }

        public static List<DummyClientWinForms.Models.CharacterSummary> ParseCharacterList(byte[] body, out bool ok, out string failReason)
        {
            var msg = LoginProto.CharacterListResponse.Parser.ParseFrom(body);
            var list = new List<DummyClientWinForms.Models.CharacterSummary>();
            ok = msg.Ok;
            failReason = msg.FailReason ?? string.Empty;
            foreach (var item in msg.Characters)
            {
                list.Add(new DummyClientWinForms.Models.CharacterSummary
                {
                    CharId = item.CharId,
                    Name = item.Name ?? string.Empty,
                    Level = item.Level,
                    Job = (ushort)item.Job,
                    AppearanceCode = item.AppearanceCode,
                    LastLoginAtEpochSec = item.LastLoginAtEpochSec,
                });
            }
            return list;
        }

        public static CharacterSelectResultModel ParseCharacterSelect(byte[] body)
        {
            var msg = LoginProto.CharacterSelectResponse.Parser.ParseFrom(body);
            var endpoint = msg.SelectedEntry != null && msg.SelectedEntry.Endpoints.Count > 0 ? msg.SelectedEntry.Endpoints[0] : null;
            return new CharacterSelectResultModel
            {
                Ok = msg.Ok,
                FailReason = msg.FailReason ?? string.Empty,
                AccountId = msg.AccountId,
                CharId = msg.CharId,
                WorldPort = endpoint != null ? (ushort)endpoint.Port : (ushort)0,
                WorldHost = endpoint?.Host ?? string.Empty,
                WorldToken = msg.WorldToken ?? string.Empty,
            };
        }

        public static EnterWorldResultModel ParseEnterWorldResult(byte[] body)
        {
            var msg = WorldProto.EnterWorldResult.Parser.ParseFrom(body);
            return new EnterWorldResultModel
            {
                Ok = msg.Ok,
                Reason = (ushort)msg.Reason,
                AccountId = msg.AccountId,
                CharId = msg.CharId,
            };
        }

        public static StatsModel ParseStats(byte[] body)
        {
            var msg = WorldProto.StatsResponse.Parser.ParseFrom(body);
            return new StatsModel
            {
                CharId = msg.CharId,
                Hp = msg.Hp,
                MaxHp = msg.MaxHp,
                Atk = msg.Atk,
                Def = msg.Def,
                Gold = msg.Gold,
            };
        }

        public static ZoneMapStateModel ParseZoneMapState(byte[] body)
        {
            var msg = WorldProto.ZoneMapState.Parser.ParseFrom(body);
            return new ZoneMapStateModel
            {
                CharId = msg.CharId,
                ZoneId = msg.ZoneId,
                MapId = msg.MapId,
                X = msg.X,
                Y = msg.Y,
                Reason = (ushort)msg.Reason,
            };
        }

        public static PlayerObject ParsePlayerSpawn(byte[] body, bool isSelf)
        {
            var charId = PacketReader.ReadUInt64(body, 0);
            return new PlayerObject { Kind = WorldObjectKind.Player, CharId = charId, Id = "player:" + charId, X = PacketReader.ReadInt32(body, 8), Y = PacketReader.ReadInt32(body, 12), Label = isSelf ? "Self" : "Player", IsSelf = isSelf };
        }

        public static List<PlayerObject> ParsePlayerSpawnBatch(byte[] body, ulong selfId)
        {
            var count = PacketReader.ReadUInt16(body, 0);
            var list = new List<PlayerObject>();
            var offset = 2;
            for (var i = 0; i < count; i++)
            {
                var charId = PacketReader.ReadUInt64(body, offset);
                list.Add(new PlayerObject { Kind = WorldObjectKind.Player, CharId = charId, Id = "player:" + charId, X = PacketReader.ReadInt32(body, offset + 8), Y = PacketReader.ReadInt32(body, offset + 12), Label = charId == selfId ? "Self" : "Player", IsSelf = charId == selfId });
                offset += 16;
            }
            return list;
        }

        public static List<ulong> ParsePlayerDespawnBatch(byte[] body)
        {
            var count = PacketReader.ReadUInt16(body, 0);
            var list = new List<ulong>();
            var offset = 2;
            for (var i = 0; i < count; i++) { list.Add(PacketReader.ReadUInt64(body, offset)); offset += 8; }
            return list;
        }

        public static MonsterSpawnResult ParseSpawnMonsterOk(byte[] body)
        {
            return new MonsterSpawnResult { MonsterId = PacketReader.ReadUInt64(body, 0), Hp = PacketReader.ReadUInt32(body, 8), Atk = PacketReader.ReadUInt32(body, 12), Def = PacketReader.ReadUInt32(body, 16) };
        }

        public static AttackResultModel ParseAttackResult(byte[] body)
        {
            return new AttackResultModel { AttackerId = PacketReader.ReadUInt64(body, 0), TargetId = PacketReader.ReadUInt64(body, 8), Damage = PacketReader.ReadUInt32(body, 16), TargetHp = PacketReader.ReadUInt32(body, 20), Killed = PacketReader.ReadUInt32(body, 24) != 0, AttackerGold = PacketReader.ReadUInt32(body, 36) };
        }
    }

    public sealed class LoginResultModel { public bool Ok; public ulong AccountId; public ulong CharId; public ushort WorldPort; public string LoginSession; public string WorldHost; public string WorldToken; public string FailReason; public int WorldEntries; }
    public sealed class WorldSelectResultModel { public bool Ok; public string FailReason; public ushort WorldId; public string ServerCode; public ushort WorldPort; public string WorldHost; }
    public sealed class CharacterSelectResultModel { public bool Ok; public string FailReason; public ulong AccountId; public ulong CharId; public ushort WorldPort; public string WorldHost; public string WorldToken; }
    public sealed class EnterWorldResultModel { public bool Ok; public ushort Reason; public ulong AccountId; public ulong CharId; }
    public sealed class StatsModel { public ulong CharId; public uint Hp; public uint MaxHp; public uint Atk; public uint Def; public uint Gold; }
    public sealed class ZoneMapStateModel { public ulong CharId; public uint ZoneId; public uint MapId; public int X; public int Y; public ushort Reason; }
    public sealed class MonsterSpawnResult { public ulong MonsterId; public uint Hp; public uint Atk; public uint Def; }
    public sealed class AttackResultModel { public ulong AttackerId; public ulong TargetId; public uint Damage; public uint TargetHp; public bool Killed; public uint AttackerGold; }
}
