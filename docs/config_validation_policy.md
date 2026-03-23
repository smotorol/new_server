# Config Validation & Normalization Policy (World/Channel)

## 목적
- 운영 환경에서 INI 오타/누락/비정상 범위 값으로 인한 런타임 불안정을 줄이기 위해,
  **파싱(parse) → 정규화(normalize) → 보정(sanity clamp) → 확정값 로그** 순서를 서버 공통 규약으로 고정한다.
- 현재 구현은 `world_runtime_network.cpp`, `channel_runtime.cpp` 기준이며,
  본 문서는 두 런타임의 설정 처리 규칙을 동일한 정책으로 정리한다.

## 표준 처리 파이프라인
1. **Parse**: INI 섹션/키를 읽는다.
2. **Normalize**: 미설정/0값에 대해 정책 기본값을 채운다.
3. **Sanity Clamp**: 최소/최대 범위를 벗어나는 값을 안전 범위로 보정한다.
4. **Operational Log**: 최종 확정된 값을 `INI(...)` 로그로 남긴다.

## 필수 검증 규칙

### 1) 샤드/풀/스레드
- `db_shard_count`는 최소 1 이상.
- `redis_shard_count`는 0 또는 미설정 시 `db_shard_count`와 동기화 후 최소 1 보정.
- `db_pool_size_per_world`, `io_thread_count`, `logic_thread_count`는 최소 1 이상.

### 2) write-behind
- `flush_interval_sec`는 최소 1 이상.
- `flush_batch_immediate`, `flush_batch_normal`은 최소 1 이상.
- `ttl_sec`는 최소 1 이상.

### 3) 세션/재접속
- `SESSION.RECONNECT_GRACE_CLOSE_DELAY_MS`는 정책 기본값(현재 5000ms)을 기준으로 로드/보정.
- 최종값을 `INI(SESSION)` 로그로 출력한다.

### 4) AOI/맵
- 맵 크기/셀 단위/반경(`map_w`, `map_h`, `world_sight_unit`, `aoi_r`)은 0/음수 입력을 허용하지 않으며,
  초기화 가능한 최소값으로 보정해야 한다.
- 정규화된 값은 전역 AOI config에 반영되고 `INI(AOI)` 로그에 남긴다.

### 5) Redis wait 옵션
- `wait_replicas`/`wait_timeout_ms`는 음수/비정상치 입력 시 0 이상으로 보정한다.

## 로그 규약
- 최소 아래 로그 라인은 유지한다.
  - `INI loaded (UTF-8)`
  - `INI(DB_WORK)`
  - `INI(WRITE_BEHIND)`
  - `INI(SESSION)` (world)
  - `INI(REDIS)`
  - `INI(AOI)`

## 향후 강화 항목 (planned follow-up)
- 키 단위 스키마 선언(필수/선택, min/max, default)을 runtime 로더 코드에서 분리된 스키마 모듈로 유지/확장.
  - 현재 구현: `runtime_ini_schema.h` + `ApplyMinPolicies` 조합으로 로더 외부 선언형 테이블 적용.
- 잘못된 설정값에 대한 fail-fast 모드(개발/스테이징)와 auto-heal 모드(운영) 분리.
  - 현재 구현: `SYSTEM.CONFIG_FAIL_FAST=1`일 때 min-policy 위반을 즉시 에러로 처리하고 부팅을 중단.
  - 기본값(`0`): fallback 보정(auto-heal) 유지.
- 설정 버전(`CONFIG_SCHEMA_VERSION`) 도입으로 롤링 배포 중 혼재값 진단 지원.
