# Session/Auth Ownership Policy (Login/World/Control)

## 1) Identity binding source of truth
- Login success identity is always `(account_id, char_id)` returned by Account(DB-auth) service.
- `sid/serial` is treated as **transport connection key only**; gameplay/session ownership is indexed by account/character identity.
- Login runtime keeps reverse indexes for account/character/login_session/world_token -> `(sid, serial)` to avoid stale sid reuse bugs.

## 2) Duplicate login policy
- Policy: **single active world session per account + per character**.
- When a newer authenticated world bind arrives, old session(s) are kicked/closed and new session becomes authoritative.
- This applies to:
  - same account different character attempts,
  - same character reconnect,
  - replay/reordered packets from stale serial.

## 3) Server role boundaries
- Login server
  - handles client credential request relay,
  - returns world endpoint + auth ticket,
  - does not own gameplay state.
- World server
  - validates/consumes auth ticket,
  - binds authoritative `(account_id, char_id)` session,
  - owns player lifecycle (`EnterPending -> InWorld -> Closing`).
- Control server
  - operational control/commands/inspection only,
  - must not mutate player authority/session ownership directly.

## 4) Reconnect semantics
- reconnect with same identity is allowed,
- previous bound session is deterministically evicted,
- stale sid with mismatched serial is ignored.

## 5) Why this policy
This keeps DB/cache/inventory/combat persistence keyed by real character identity and prevents sid reuse from corrupting account/character ownership.

## 6) Implementation progress (Phase 2)
- World auth session reverse indexes now store packed session key `(sid, serial)` instead of sid-only.
- Old-session lookup validates both sid and serial before considering a duplicate/eviction target.

## 7) Implementation progress (Phase 3)
- ZoneActor move path now uses entered/left edge-cell based visibility delta for 1-cell moves.
- World move handler consumes precomputed `entered_vis` / `exited_vis` first, and only falls back to full old/new set-diff for non-local jumps.

## 8) Implementation progress (Phase 4)
- Added AOI operational counters:
  - `moves/s`, `fanout/s`, `avg_fanout`, `entered/s`, `exited/s`
- World main loop now emits `[aoistats]` log lines to support production bottleneck diagnosis with numeric signals.

## 9) Implementation progress (Phase 5)
- World handler now rejects gameplay packets from unauthenticated sessions (no fallback `sid -> actor` mapping).
- Added `unauth_packet_rejects/s` operational warning log to detect auth/session race or abuse traffic.

## 10) Implementation progress (Phase 6+)
- Duplicate-login close path now increments operational counters by category:
  `duplicate_char`, `duplicate_account`, `duplicate_both`, `deduplicated_same_session`.
- Shutdown sequence is now logged step-by-step (accept stop → final dirty flush enqueue → DB stop → actor stop).
- `flush_one_char` payload/result includes version fields and returns `conflict` on stale version mismatch.
- Spawn/Despawn notify path to mover uses batch packets (`player_spawn_batch` / `player_despawn_batch`).
- Auth reject logging now has threshold warning mode (`>=10/s`) with sampled source sid.
- `flush_dirty_chars` path now checks cached expected-version and reports conflict/skip on mismatch.

## 11) Planned follow-up (mobile/pc reconnect resilience)
- Reconnect resume token flow for IP/network path changes:
  - short-lived resume token bound to `(account_id, char_id, device/session fingerprint)`,
  - old transport sid/serial invalidation with grace window.
- Session takeover policy for intermittent disconnect:
  - hold authoritative actor state for N seconds before final teardown,
  - allow seamless reclaim by same identity during grace period.
- Multi-platform policy:
  - per-account concurrent platform rules (mobile+pc allowed or exclusive),
  - explicit conflict/kick semantics and audit logs.
