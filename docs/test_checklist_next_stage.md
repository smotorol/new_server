# Next Stage Test Checklist (Session/AOI/Flush/Reconnect)

## A. Session & reconnect
- [ ] Disconnect then reconnect within grace window (`kReconnectGraceCloseDelay_`) and verify session reclaim.
- [ ] Disconnect then reconnect after grace window and verify full teardown + clean re-enter.
- [ ] Simulate IP change (mobile LTE <-> Wi-Fi) and verify reconnect flow remains account/char authoritative.
- [ ] Verify duplicate-login category counters increment correctly (`char/account/both/dedup_same`).
- [x] `smoke_persistence_shutdown` 정적 smoke에 reconnect grace(reserve/arm/log 경로) 코드 존재 점검 추가.
- [x] `smoke_persistence_shutdown` 정적 smoke에 duplicate-login category 카운터 증가 코드/`dupstats` 로그 코드 존재 점검 추가.
- [x] `SESSION.RECONNECT_GRACE_CLOSE_DELAY_MS` 설정값 파싱/정상화/로그 경로 추가.

## B. AOI broadcast
- [ ] 1-cell moves produce expected entered/exited behavior.
- [ ] Mover receives `player_spawn_batch` / `player_despawn_batch` with valid count/body size.
- [ ] Recipients around mover still receive expected self spawn/despawn + move stream.
- [ ] Validate no malformed/zeroed entries are emitted in normal populated cases.
- [x] `world_regression_tests`에서 spawn/despawn batch 메모리 레이아웃 및 count/item 접근 검증(정적/로컬 회귀).
- [x] `smoke_persistence_shutdown` 정적 smoke에 AOI 브로드캐스트 malformed guard(`char_id==0` 필터, exited sanitize, recipient zero-id skip) 코드 존재 점검 추가.
- [x] `world_regression_tests`에 AOI id sanitize(0/중복 제거) + batch count/body-size helper 회귀 추가.

## C. Persistence / flush
- [ ] `flush_one_char` succeeds when expected_version == actual_version.
- [ ] `flush_one_char` returns `conflict` when expected_version != actual_version.
- [ ] conflict logs contain world/char/expected/actual version fields.
- [ ] `flush_dirty_chars` version conflict 경로(충돌 카운트/dirty 재마킹) 및 throughput 회귀 확인.
- [x] `smoke_persistence_shutdown` ctest 시나리오로 `flush_dirty_chars` conflict-guard 코드 경로 존재를 자동 점검.
- [x] `smoke_persistence_shutdown` 정적 smoke에 `FlushOneCharConflict`/`FlushDirtyCharsConflict` 로그 포맷(핵심 필드) 존재 점검 추가.
- [x] `world_regression_tests`에서 `FlushDirtyCharsResult`의 `shard_id/conflicts` 결과 필드 shape/기본 합계 일관성 점검.

## D. Shutdown
- [ ] `OnBeforeIoStop` emits ordered shutdown logs.
- [ ] DQS in-flight count drains to zero before DB worker stop (or timeout flag appears).
- [ ] Timeout path leaves deterministic logs and no deadlock.
- [x] `smoke_persistence_shutdown` ctest 시나리오로 shutdown step 로그 marker 순서를 정적 smoke 검증.
- [x] `smoke_persistence_shutdown` 정적 smoke에 `wait_dqs_drain_end in_flight/timed_out` 로그 경로 존재 점검 추가.
- [x] timeout 분기 경고 로그(`[shutdown] dqs drain timed out ...`) 코드 경로 정적 smoke 점검 추가.

## E. Auth hardening
- [ ] Unauthenticated gameplay packets are rejected and counted.
- [ ] `authstats` warning triggers when reject rate exceeds threshold.
- [x] `smoke_persistence_shutdown` 정적 smoke에 unauth reject 계수 증가 코드/`authstats` 임계치 로그 코드 존재 점검 추가.

## F. CI gate
- [x] `tests/run_ci_ctest.sh`로 기본 PR 게이트(`world_regression_tests`, `smoke_persistence_shutdown`) 묶음 실행.

## G. Config validation policy
- [x] INI 파싱/정규화/보정/확정값 로깅 규약 문서(`docs/config_validation_policy.md`) 추가.
- [ ] 선언형 스키마 기반 validation/fail-fast 모드 분리 구현.
