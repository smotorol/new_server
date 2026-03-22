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
