using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using DummyClientWinForms.Models;

namespace DummyClientWinForms.Services
{
    public sealed class GameDataService
    {
        private const uint Magic = 0x31444E5A;
        private const uint Version = 1;

        private readonly string _binaryPath;
        private readonly List<ZoneMapRecord> _maps = new List<ZoneMapRecord>();
        private readonly List<ZonePortalRecord> _portals = new List<ZonePortalRecord>();
        private readonly List<ZoneNpcRecord> _npcs = new List<ZoneNpcRecord>();
        private readonly List<ZoneMonsterRecord> _monsters = new List<ZoneMonsterRecord>();
        private readonly List<ZoneSafeRecord> _safe = new List<ZoneSafeRecord>();
        private readonly List<ZoneSpecialRecord> _special = new List<ZoneSpecialRecord>();

        public GameDataService(string binaryPath = null)
        {
            _binaryPath = !string.IsNullOrWhiteSpace(binaryPath)
                ? binaryPath
                : (Environment.GetEnvironmentVariable("DC_ZONE_RUNTIME_DATA_PATH")
                    ?? Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "resources", "zone_runtime.bin"));
            _binaryPath = Path.GetFullPath(_binaryPath);
            TryLoad();
        }

        public string DataRoot => _binaryPath;

        public List<WorldObject> LoadZoneOverlay(int zoneId)
        {
            var list = new List<WorldObject>();
            list.AddRange(_npcs.Where(x => x.ZoneId == zoneId).Select((r, i) => new NpcObject
            {
                Id = $"npc:{zoneId}:{r.RegionId}:{i}",
                Kind = WorldObjectKind.Npc,
                Label = $"NPC {r.Value02}",
                ZoneId = (int)r.ZoneId,
                MapId = (int)r.MapId,
                X = r.CenterX,
                Y = r.CenterZ,
                Heading = r.Value03,
                Radius = 0,
                NpcTemplateId = r.Value02,
                IsStaticOverlay = true,
            }));
            list.AddRange(_monsters.Where(x => x.ZoneId == zoneId).Select((r, i) => new MonsterObject
            {
                Id = $"monster:{zoneId}:{r.RegionId}:{i}",
                Kind = WorldObjectKind.Monster,
                Label = $"Monster {r.Value02}-{r.Value03} x{r.Value04}",
                ZoneId = (int)r.ZoneId,
                MapId = (int)r.MapId,
                X = r.CenterX,
                Y = r.CenterZ,
                Radius = r.Radius,
                MonsterTemplateMin = r.Value02,
                MonsterTemplateMax = r.Value03,
                SpawnCount = r.Value04,
                IsStaticOverlay = true,
            }));
            list.AddRange(_portals.Where(x => x.ZoneId == zoneId).Select((r, i) => new PortalObject
            {
                Id = $"portal:{zoneId}:{r.RegionId}:{i}",
                Kind = WorldObjectKind.Portal,
                Label = $"Portal -> {r.DestZoneId}/{r.DestMapId}",
                ZoneId = (int)r.ZoneId,
                MapId = (int)r.MapId,
                X = r.CenterX,
                Y = r.CenterZ,
                Radius = r.Radius,
                RawValue01 = r.Value01,
                RawValue02 = r.Value02,
                RawValue03 = r.Value03,
                RawValue04 = r.Value04,
                IsStaticOverlay = true,
            }));
            return list;
        }

        private void TryLoad()
        {
            if (!File.Exists(_binaryPath))
            {
                return;
            }

            using (var fs = File.OpenRead(_binaryPath))
            using (var br = new BinaryReader(fs))
            {
                var magic = br.ReadUInt32();
                var version = br.ReadUInt32();
                if (magic != Magic || version != Version)
                {
                    return;
                }

                var mapCount = br.ReadUInt32();
                var portalCount = br.ReadUInt32();
                var npcCount = br.ReadUInt32();
                var monsterCount = br.ReadUInt32();
                var safeCount = br.ReadUInt32();
                var specialCount = br.ReadUInt32();

                for (var i = 0; i < mapCount; i++) _maps.Add(new ZoneMapRecord { ZoneId = br.ReadUInt32(), MapId = br.ReadUInt32() });
                for (var i = 0; i < portalCount; i++) _portals.Add(new ZonePortalRecord(br));
                for (var i = 0; i < npcCount; i++) _npcs.Add(new ZoneNpcRecord(br));
                for (var i = 0; i < monsterCount; i++) _monsters.Add(new ZoneMonsterRecord(br));
                for (var i = 0; i < safeCount; i++) _safe.Add(new ZoneSafeRecord(br));
                for (var i = 0; i < specialCount; i++) _special.Add(new ZoneSpecialRecord(br));
            }
        }

        private sealed class ZoneMapRecord
        {
            public uint ZoneId;
            public uint MapId;
        }

        private class ZoneRegionBase
        {
            protected ZoneRegionBase() { }
            protected ZoneRegionBase(BinaryReader br)
            {
                ZoneId = br.ReadUInt32();
                MapId = br.ReadUInt32();
                RegionId = br.ReadUInt32();
                Value01 = br.ReadInt32();
                Value02 = br.ReadInt32();
                Value03 = br.ReadInt32();
                Value04 = br.ReadInt32();
                CenterX = br.ReadInt32();
                CenterY = br.ReadInt32();
                CenterZ = br.ReadInt32();
                Radius = br.ReadInt32();
            }
            public uint ZoneId;
            public uint MapId;
            public uint RegionId;
            public int Value01;
            public int Value02;
            public int Value03;
            public int Value04;
            public int CenterX;
            public int CenterY;
            public int CenterZ;
            public int Radius;
        }

        private sealed class ZonePortalRecord : ZoneRegionBase
        {
            public ZonePortalRecord(BinaryReader br) : base(br)
            {
                DestZoneId = br.ReadUInt32();
                DestMapId = br.ReadUInt32();
                DestX = br.ReadInt32();
                DestY = br.ReadInt32();
                DestZ = br.ReadInt32();
            }
            public uint DestZoneId;
            public uint DestMapId;
            public int DestX;
            public int DestY;
            public int DestZ;
        }

        private sealed class ZoneNpcRecord : ZoneRegionBase { public ZoneNpcRecord(BinaryReader br) : base(br) { } }
        private sealed class ZoneMonsterRecord : ZoneRegionBase { public ZoneMonsterRecord(BinaryReader br) : base(br) { } }
        private sealed class ZoneSafeRecord : ZoneRegionBase { public ZoneSafeRecord(BinaryReader br) : base(br) { } }
        private sealed class ZoneSpecialRecord : ZoneRegionBase { public ZoneSpecialRecord(BinaryReader br) : base(br) { } }
    }
}
