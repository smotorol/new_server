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

Legacy packed struct parsing remains for:
- heal/add_gold
- spawn/attack
- player spawn/despawn/move batch
- other AOI/combat/debug packets

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
