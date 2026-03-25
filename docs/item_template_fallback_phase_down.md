# Item Template Fallback Phase-Down

## Current State

- Authoritative runtime source: `NFX_GAME.game.item_template` preload
- Fallback/reference source: legacy `005_00002.csv` bootstrap
- Default policy: `allow_legacy_item_template_fallback = true`
- Goal: remove silent dependence before removing fallback itself

## Verification Signals

- startup preload source: `db | legacy_csv | empty`
- preload row count
- repository empty 여부
- fallback entered 여부
- repository ready 여부
- last error reason
- template miss count
- first sampled miss item ids

## Runtime Observation Points

- startup log: preload result summary
- item provider warn: repository empty / first template miss samples
- enter debug log: `char_id`, `level/job/tribe`, equip template ids, source summary, final combat stats
- stats debug log: source summary + final combat stats

## Control Flag

- env: `DC_ALLOW_LEGACY_ITEM_TEMPLATE_FALLBACK`
- default: `true`
- `0`, `f`, `F` 로 시작하면 fallback 비활성화
- 권장 순서:
  1. production 에서는 기본 `true`
  2. staging 에서 preload count / miss count 안정화 확인
  3. 이후 `false` 검증

## Runtime Status Helper

`ItemTemplateRepository::SnapshotStatus()` 로 확인 가능:
- `source`
- `preload_count`
- `miss_count`
- `fallback_entered`
- `fallback_allowed`
- `ready`
- `empty`
- `last_error_reason`
- `miss_samples`

## Phase-Down Steps

1. Phase 1: Observe
   - preload count > 0 확인
   - fallback entered 여부와 reason 수집
   - template miss count 추적
2. Phase 2: Stabilize Import
   - `item_template_import`로 운영 DB seed/update 반복
   - preload source가 지속적으로 `db`가 되는지 확인
3. Phase 3: Tighten Warning
   - fallback 진입 시 warn 유지
   - miss sample item ids를 import backlog와 대조
4. Phase 4: Disable Option Ready
   - `DC_ALLOW_LEGACY_ITEM_TEMPLATE_FALLBACK=0` 검증
   - staging에서 empty/error 시 동작 확인
5. Phase 5: Remove Decision
   - preload count 안정
   - fallback rate 사실상 0
   - miss count가 수용 가능한 수준일 때 제거 판단

## Operational Checklist

- `game.item_template` row count > 0
- world startup log에서 `source=db` 확인
- `fallback_entered=0` 확인
- template miss count 증가 여부 확인
- enter/stats debug 로그에서 equip template id와 source summary 확인
- fallback 비활성화 전 `last_error_reason`이 비정상적으로 남지 않는지 확인

## Rollback / Emergency

- import 실패 시 fallback 유지
- preload source가 empty/db_error면 import 재실행
- 긴급 대응으로 `DC_ALLOW_LEGACY_ITEM_TEMPLATE_FALLBACK=1` 유지
- fallback 제거 전까지 runtime hot path는 DB preload 우선, legacy fallback 보조 정책 유지
