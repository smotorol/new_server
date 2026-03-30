# Zone Content Runtime Apply

## Runtime source of truth
- `data_src/zone_csv/*` -> builder -> `resources/zone_runtime.bin` -> `ZoneRuntimeDataStore`
- no direct runtime dependency on legacy TS_ZONE source/data paths

## Applied in this step
- map existence validation helper: `HasMap(zone_id, map_id)`
- portal trigger lookup already used by world move handler
- safe/special region lookup helpers added
- npc/monster region query helpers added
- zone runtime now validates map assignment against runtime data
- zone runtime logs region summary on player enter

## Still debug/test level
- npc/monster regions are queryable but not yet full authoritative spawn simulation
- special/safe regions are exposed as helpers/logging, not full gameplay policy system
- portal transition is still partly world-side debug/test oriented
