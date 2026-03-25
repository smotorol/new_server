using System;
using System.Collections.Generic;
using DummyClientWinForms.Models;

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
            using (var w = new PacketWriter())
            {
                w.WriteFixedString(loginId, LoginIdLen);
                w.WriteFixedString(password, PasswordLen);
                return w.ToArray();
            }
        }

        public static byte[] BuildWorldListRequest() => new byte[] { 0 };
        public static byte[] BuildWorldSelectRequest(ushort worldId, ushort channelId) { using (var w = new PacketWriter()) { w.WriteUInt16(worldId); w.WriteUInt16(channelId); return w.ToArray(); } }
        public static byte[] BuildCharacterListRequest() => new byte[] { 0 };
        public static byte[] BuildCharacterSelectRequest(ulong charId) { using (var w = new PacketWriter()) { w.WriteUInt64(charId); return w.ToArray(); } }
        public static byte[] BuildEnterWorldRequest(ulong accountId, string loginSession, string worldToken)
        {
            using (var w = new PacketWriter())
            {
                w.WriteUInt64(accountId);
                w.WriteUInt64(0);
                w.WriteFixedString(loginSession, LoginSessionLen);
                w.WriteFixedString(worldToken, WorldTokenLen);
                return w.ToArray();
            }
        }
        public static byte[] BuildGetStats() { using (var w = new PacketWriter()) { w.WriteUInt32(0); return w.ToArray(); } }
        public static byte[] BuildMove(int x, int y) { using (var w = new PacketWriter()) { w.WriteInt32(x); w.WriteInt32(y); return w.ToArray(); } }
        public static byte[] BuildHeal(uint amount) { using (var w = new PacketWriter()) { w.WriteUInt32(amount); return w.ToArray(); } }
        public static byte[] BuildGold(uint amount) { using (var w = new PacketWriter()) { w.WriteUInt32(amount); return w.ToArray(); } }
        public static byte[] BuildSpawnMonster(uint templateId) { using (var w = new PacketWriter()) { w.WriteUInt32(templateId); return w.ToArray(); } }
        public static byte[] BuildAttackMonster(ulong monsterId) { using (var w = new PacketWriter()) { w.WriteUInt64(monsterId); return w.ToArray(); } }

        public static LoginResultModel ParseLoginResult(byte[] body)
        {
            return new LoginResultModel
            {
                Ok = body[0] != 0,
                AccountId = PacketReader.ReadUInt64(body, 1),
                CharId = PacketReader.ReadUInt64(body, 9),
                WorldPort = PacketReader.ReadUInt16(body, 17),
                LoginSession = PacketReader.ReadFixedString(body, 19, LoginSessionLen),
                WorldHost = PacketReader.ReadFixedString(body, 84, WorldHostLen),
                WorldToken = PacketReader.ReadFixedString(body, 149, WorldTokenLen),
            };
        }

        public static List<WorldSummary> ParseWorldList(byte[] body, out bool ok, out string failReason)
        {
            var list = new List<WorldSummary>();
            ok = body[0] != 0;
            var count = PacketReader.ReadUInt16(body, 1);
            failReason = PacketReader.ReadFixedString(body, 3, FailReasonLen);
            var offset = 68;
            for (var i = 0; i < count && i < 16; i++)
            {
                list.Add(new WorldSummary
                {
                    WorldId = PacketReader.ReadUInt16(body, offset),
                    ChannelId = PacketReader.ReadUInt16(body, offset + 2),
                    ActiveZoneCount = PacketReader.ReadUInt16(body, offset + 4),
                    LoadScore = PacketReader.ReadUInt16(body, offset + 6),
                    PublicPort = PacketReader.ReadUInt16(body, offset + 8),
                    Flags = PacketReader.ReadUInt32(body, offset + 10),
                    ServerName = PacketReader.ReadFixedString(body, offset + 14, ServiceNameLen),
                    PublicHost = PacketReader.ReadFixedString(body, offset + 47, WorldHostLen),
                });
                offset += 112;
            }
            return list;
        }

        public static WorldSelectResultModel ParseWorldSelect(byte[] body)
        {
            return new WorldSelectResultModel
            {
                Ok = body[0] != 0,
                FailReason = PacketReader.ReadUInt16(body, 1),
                WorldId = PacketReader.ReadUInt16(body, 3),
                ChannelId = PacketReader.ReadUInt16(body, 5),
                WorldPort = PacketReader.ReadUInt16(body, 7),
                WorldHost = PacketReader.ReadFixedString(body, 9, WorldHostLen),
            };
        }

        public static List<CharacterSummary> ParseCharacterList(byte[] body, out bool ok, out string failReason)
        {
            var list = new List<CharacterSummary>();
            ok = body[0] != 0;
            var count = PacketReader.ReadUInt16(body, 1);
            failReason = PacketReader.ReadFixedString(body, 3, FailReasonLen);
            var offset = 68;
            for (var i = 0; i < count && i < 8; i++)
            {
                list.Add(new CharacterSummary
                {
                    CharId = PacketReader.ReadUInt64(body, offset),
                    Name = PacketReader.ReadFixedString(body, offset + 8, CharNameLen),
                    Level = PacketReader.ReadUInt32(body, offset + 29),
                    Job = PacketReader.ReadUInt16(body, offset + 33),
                    AppearanceCode = PacketReader.ReadUInt32(body, offset + 35),
                    LastLoginAtEpochSec = PacketReader.ReadUInt64(body, offset + 39),
                });
                offset += 47;
            }
            return list;
        }

        public static CharacterSelectResultModel ParseCharacterSelect(byte[] body)
        {
            return new CharacterSelectResultModel
            {
                Ok = body[0] != 0,
                FailReason = PacketReader.ReadUInt16(body, 1),
                AccountId = PacketReader.ReadUInt64(body, 3),
                CharId = PacketReader.ReadUInt64(body, 11),
                WorldPort = PacketReader.ReadUInt16(body, 19),
                WorldHost = PacketReader.ReadFixedString(body, 21, WorldHostLen),
                WorldToken = PacketReader.ReadFixedString(body, 86, WorldTokenLen),
            };
        }

        public static EnterWorldResultModel ParseEnterWorldResult(byte[] body)
        {
            return new EnterWorldResultModel
            {
                Ok = body[0] != 0,
                Reason = PacketReader.ReadUInt16(body, 1),
                AccountId = PacketReader.ReadUInt64(body, 3),
                CharId = PacketReader.ReadUInt64(body, 11),
            };
        }

        public static StatsModel ParseStats(byte[] body)
        {
            return new StatsModel
            {
                CharId = PacketReader.ReadUInt64(body, 0),
                Hp = PacketReader.ReadUInt32(body, 8),
                MaxHp = PacketReader.ReadUInt32(body, 12),
                Atk = PacketReader.ReadUInt32(body, 16),
                Def = PacketReader.ReadUInt32(body, 20),
                Gold = PacketReader.ReadUInt32(body, 24),
            };
        }

        public static ZoneMapStateModel ParseZoneMapState(byte[] body)
        {
            return new ZoneMapStateModel
            {
                CharId = PacketReader.ReadUInt64(body, 0),
                ZoneId = PacketReader.ReadUInt32(body, 8),
                MapId = PacketReader.ReadUInt32(body, 12),
                X = PacketReader.ReadInt32(body, 16),
                Y = PacketReader.ReadInt32(body, 20),
                Reason = PacketReader.ReadUInt16(body, 24),
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

    public sealed class LoginResultModel { public bool Ok; public ulong AccountId; public ulong CharId; public ushort WorldPort; public string LoginSession; public string WorldHost; public string WorldToken; }
    public sealed class WorldSelectResultModel { public bool Ok; public ushort FailReason; public ushort WorldId; public ushort ChannelId; public ushort WorldPort; public string WorldHost; }
    public sealed class CharacterSelectResultModel { public bool Ok; public ushort FailReason; public ulong AccountId; public ulong CharId; public ushort WorldPort; public string WorldHost; public string WorldToken; }
    public sealed class EnterWorldResultModel { public bool Ok; public ushort Reason; public ulong AccountId; public ulong CharId; }
    public sealed class StatsModel { public ulong CharId; public uint Hp; public uint MaxHp; public uint Atk; public uint Def; public uint Gold; }
    public sealed class ZoneMapStateModel { public ulong CharId; public uint ZoneId; public uint MapId; public int X; public int Y; public ushort Reason; }
    public sealed class MonsterSpawnResult { public ulong MonsterId; public uint Hp; public uint Atk; public uint Def; }
    public sealed class AttackResultModel { public ulong AttackerId; public ulong TargetId; public uint Damage; public uint TargetHp; public bool Killed; public uint AttackerGold; }
}
