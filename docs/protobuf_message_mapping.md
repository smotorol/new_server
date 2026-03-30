# Protobuf Message Mapping

## Client-facing protobuf switched

### Login / directory
- msg_id `1001` -> `dc.proto.client.login.LoginRequest`
- msg_id `1101` -> `dc.proto.client.login.LoginResult`
- msg_id `1002` -> `dc.proto.client.login.WorldListRequest`
- msg_id `1102` -> `dc.proto.client.login.WorldListResponse`
- msg_id `1003` -> `dc.proto.client.login.WorldSelectRequest`
- msg_id `1103` -> `dc.proto.client.login.WorldSelectResponse`
- msg_id `1004` -> `dc.proto.client.login.CharacterListRequest`
- msg_id `1104` -> `dc.proto.client.login.CharacterListResponse`
- msg_id `1005` -> `dc.proto.client.login.CharacterSelectRequest`
- msg_id `1105` -> `dc.proto.client.login.CharacterSelectResponse`

### World gameplay entry / status
- msg_id `2001` -> `dc.proto.client.world.EnterWorldWithTokenRequest`
- msg_id `2101` -> `dc.proto.client.world.EnterWorldResult`
- msg_id `10` -> `dc.proto.client.world.GetStatsRequest` / `dc.proto.client.world.StatsResponse`
- msg_id `12` -> `dc.proto.client.world.ZoneMapState`
- msg_id `40` -> `dc.proto.client.world.MoveRequest`
- msg_id `11` -> `dc.proto.client.world.HealSelfRequest` / `dc.proto.client.world.StatsResponse`
- msg_id `2` -> `dc.proto.client.world.AddGoldRequest` / `dc.proto.client.world.AddGoldResult`
- msg_id `20` -> `dc.proto.client.world.SpawnMonsterRequest` / `dc.proto.client.world.SpawnMonsterResult`
- msg_id `21` -> `dc.proto.client.world.AttackMonsterRequest` / `dc.proto.client.world.AttackResult`
- msg_id `30` -> `dc.proto.client.world.AttackPlayerRequest` / `dc.proto.client.world.AttackResult`
- msg_id `40` -> `dc.proto.client.world.PlayerSpawn` (S2C direction)
- msg_id `41` -> `dc.proto.client.world.PlayerDespawn`
- msg_id `42` -> `dc.proto.client.world.PlayerMove`
- msg_id `43` -> `dc.proto.client.world.PlayerMoveBatch`
- msg_id `44` -> `dc.proto.client.world.PlayerSpawnBatch`
- msg_id `45` -> `dc.proto.client.world.PlayerDespawnBatch`

## World entry metadata shape
`dc.proto.common.WorldEntryNode`
- `world_id`
- `server_code`
- `display_name`
- `region`
- `status`
- `recommended`
- `population`
- `capacity`
- `transfer_policy`
- `endpoints[]`

## Mixed-mode rule
- client-facing migrated messages: protobuf parse first, legacy fallback
- client-facing migrated responses: protobuf if originating request set `use_protobuf`, otherwise legacy struct
- server-to-server messages: still legacy packed struct only

## Still not switched
- internal login-account/login-world/world-zone/account-world bodies
- remaining unused client-facing debug packets not exercised by DummyClientWinForms

