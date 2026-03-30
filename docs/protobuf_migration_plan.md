# Protobuf Migration Plan

## Goal
Keep the existing 4-byte `_MSG_HEADER` framing and migrate only packet bodies to protobuf bytes.

## Current client-facing switched scope
Completed in code and build:
- login request/result
- world list request/result
- world select request/result
- character list request/result
- character select request/result
- world enter with token request/result
- stats request/response
- move request
- zone_map_state response/notify
- heal_self / add_gold
- spawn_monster / attack_monster / attack_player
- player_spawn / player_despawn / player_move
- player_spawn_batch / player_despawn_batch / player_move_batch

## Mixed mode
- migrated client-facing messages use protobuf-first parse with legacy fallback
- migrated responses follow session/request `use_protobuf`
- non-migrated client-facing messages remain packed struct
- internal server-to-server messages remain packed struct in this phase

## Framing rule
- header: `_MSG_HEADER` unchanged
- `m_wSize`: header + protobuf body size
- `m_byType`: existing `msg_id`
- body: protobuf serialized payload

## Dependency bring-up
- C++ protobuf runtime/codegen comes from vcpkg `protobuf:x64-windows-static`
- generated C++ output: `src/proto/generated/cpp`
- generated C# output: `tools/proto_generated/csharp`
- regeneration target: `dc_proto_codegen`

## Safe next order
1. WinForms runtime regression for mixed protobuf/legacy sessions
2. Unity runtime package wiring + first-path live test
3. reconnect/resume token path formalization on protobuf client path
4. internal login-account first-path only if/when needed separately

