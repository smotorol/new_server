# Connection Architecture 1st Target

## Decision
Prefer:
`login/account -> world directory -> gateway/world-front entry -> world/zone internal routing`

## Why not world direct entry as final target
- single world node becomes connection hotspot
- reconnect/session recovery is harder when gameplay and entry proxy are the same node
- scale-out for mobile/PC/console concurrency is limited

## 1st realistic target for current codebase
- login: public auth entry
- account: account/session authority + world directory aggregation
- world-front: public gameplay entry, token validation, reconnect handoff, routing
- world/zone internal: gameplay/session state + simulation

## World list metadata
A world list entry should represent:
- logical world identity
- display metadata
- status/capacity
- one or more physical entry endpoints
- reconnect/transfer policy

## Scale-out notes
- logical `world_id` must be separate from entry-node instance id
- reconnect should prefer token-based resume over same-socket/node affinity when possible
- L4/L7 or DNS RR can sit in front of world-front nodes
