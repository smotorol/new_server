# DummyClientWinForms Protobuf Path

## Current scope
The WinForms dummy client now uses generated protobuf C# for:
- login request/result
- world list request/result
- world select request/result
- character list request/result
- character select request/result
- world enter request/result
- stats request/response
- move request
- zone_map_state response/notify
- heal/add_gold
- spawn_monster / attack_monster / attack_player
- player spawn / player despawn / player move
- player spawn batch / player despawn batch / player move batch

Legacy packed struct parsing remains only as fallback when protobuf parse fails or when the connected session is legacy mode.

## Runtime dependencies
Current net48-compatible references:
- `Google.Protobuf` from local NuGet cache: `3.21.9/lib/net45`
- `System.Memory` from local NuGet cache: `4.5.4/lib/net461`

## Generated code
- source path: `tools/proto_generated/csharp`
- included into csproj as linked files

## Execute
1. Build `DummyClientWinForms.csproj`
2. Run `tools/DummyClientWinForms/bin/Debug/DummyClientWinForms.exe`
3. Test flow: connect -> login -> world list -> world select -> character list -> character select -> enter world -> stats/move
4. Gameplay checks: heal, gold, spawn monster, attack, AOI move batch

