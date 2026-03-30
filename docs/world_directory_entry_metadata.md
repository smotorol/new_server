# World Directory Entry Metadata

## Current protobuf shape
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

`dc.proto.common.Endpoint`
- `host`
- `port`
- `region`

## Current first-path use
- login result may include world entry summaries
- world list response returns repeated `WorldEntryNode`
- world select response returns `selected_entry`

## Architectural intent
This keeps logical world identity separate from physical entry endpoint and leaves room for gateway/world-front expansion.
