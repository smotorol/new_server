# Current Packet Structure Analysis

## Framing
- `_MSG_HEADER` in `src/net/packet/msg_header.h`
- 4 bytes total
- `m_wSize`: total packet size including header
- `m_byType[2]`: little-endian msg id

## Current body encoding
- packed C++ struct
- fixed-length strings
- `proto::as<T>` reinterpret-cast decode in handlers

## Fragile points
- ABI/packing dependence across languages
- fixed string truncation/padding drift
- DummyClientWinForms manual offset parsing duplicates protocol knowledge
- every client language must hand-maintain the same field order/size

## DummyClientWinForms hotspots
- `Network/PacketReader.cs`
- `Network/PacketWriter.cs`
- `Network/ClientProtocol.cs`
- fixed offsets for login/world list/character list/world enter/stats/zone_map_state

## Safe migration rule
Keep msg id + header, replace only body encoding.
