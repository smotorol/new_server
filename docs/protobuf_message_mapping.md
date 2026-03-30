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
- heal / add_gold / spawn / attack
- player spawn/despawn/move batch
- internal login-account/login-world/world-zone/account-world bodies
