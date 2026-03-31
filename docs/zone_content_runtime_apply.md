# Zone Content Runtime Apply

## Runtime source of truth
- runtime gameplay source remains `resources/zone_runtime.bin` loaded by `ZoneRuntimeDataStore`
- no direct runtime dependency on legacy `G:\Programing\Work\12sky1\...` paths
- readable internal content copy now lives under `resources/map/zone_001/*`

## Data format decision
- WM geometry: kept as raw binary `map.wm` inside `resources/map/zone_001/`
- reason: legacy WM is compressed geometry + quadtree data and is not a small/safe CSV conversion target for this ticket
- region content: kept as readable CSV (`maps.csv`, `portal.csv`, `npc.csv`, `monster.csv`, `safe.csv`, `special.csv`)
- runtime simulation still uses `zone_runtime.bin`; CSV is for ownership/debugging and future tooling

## Applied in this step
- world startup now scans `resources/map/*` via `zone_content_catalog`
- startup logs zone/map content counts and raw WM presence
- enter world logs which internal content package backs the current zone/map
- monster debug spawn now derives a template from the nearest monster region when request template is `0`
- safe regions now actively block `attack_monster`, `attack_player`, and debug `spawn_monster`

## Current packaged zone
- `resources/map/zone_001/map.wm`
- `resources/map/zone_001/maps.csv`
- `resources/map/zone_001/portal.csv`
- `resources/map/zone_001/npc.csv`
- `resources/map/zone_001/monster.csv`
- `resources/map/zone_001/safe.csv`
- `resources/map/zone_001/special.csv`

Zone 001 counts:
- portals: 6
- npcs: 49
- monster regions: 212
- safe regions: 5
- special regions: 23

## Still debug/test level
- WM geometry is preserved but not yet used for collision/height/pathing
- NPC/monster regions are available and visible in DummyClient overlay, but monster lifecycle is still debug-driven rather than full authoritative respawn simulation
- portal runtime still relies on `zone_runtime.bin` records and existing move/transition hooks
- safe region policy currently blocks combat/spawn only; it is not yet a full ruleset system
