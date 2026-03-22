# Next Stage Test Checklist (Session/AOI/Flush/Reconnect)

## A. Session & reconnect
- [ ] Disconnect then reconnect within grace window (`kReconnectGraceCloseDelay_`) and verify session reclaim.
- [ ] Disconnect then reconnect after grace window and verify full teardown + clean re-enter.
- [ ] Simulate IP change (mobile LTE <-> Wi-Fi) and verify reconnect flow remains account/char authoritative.
- [ ] Verify duplicate-login category counters increment correctly (`char/account/both/dedup_same`).

## B. AOI broadcast
- [ ] 1-cell moves produce expected entered/exited behavior.
- [ ] Mover receives `player_spawn_batch` / `player_despawn_batch` with valid count/body size.
- [ ] Recipients around mover still receive expected self spawn/despawn + move stream.
- [ ] Validate no malformed/zeroed entries are emitted in normal populated cases.

## C. Persistence / flush
- [ ] `flush_one_char` succeeds when expected_version == actual_version.
- [ ] `flush_one_char` returns `conflict` when expected_version != actual_version.
- [ ] conflict logs contain world/char/expected/actual version fields.
- [ ] `flush_dirty_chars` version conflict 경로(충돌 카운트/dirty 재마킹) 및 throughput 회귀 확인.
- [x] `smoke_persistence_shutdown` ctest 시나리오로 `flush_dirty_chars` conflict-guard 코드 경로 존재를 자동 점검.

## D. Shutdown
- [ ] `OnBeforeIoStop` emits ordered shutdown logs.
- [ ] DQS in-flight count drains to zero before DB worker stop (or timeout flag appears).
- [ ] Timeout path leaves deterministic logs and no deadlock.
- [x] `smoke_persistence_shutdown` ctest 시나리오로 shutdown step 로그 marker 순서를 정적 smoke 검증.

## E. Auth hardening
- [ ] Unauthenticated gameplay packets are rejected and counted.
- [ ] `authstats` warning triggers when reject rate exceeds threshold.
