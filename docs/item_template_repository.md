# Item Template Repository

## 변경 이유

기존 구조는 `character_combat_stat_calculator.cpp` 내부 direct csv load 를 제거했더라도, canonical `ItemTemplateTable` 의 실제 source 가 여전히 legacy `005_00002.csv` 였다. 이 구조는 운영 환경에서 legacy data path 가 없을 때 fallback 으로 바로 내려가므로 production-grade source-of-truth 로 보기 어렵다.

이번 티켓에서는 authoritative source 를 `NFX_GAME.game.item_template` 로 올리고, world runtime 초기화 시 preload 하도록 바꿨다. legacy csv loader 는 이제 runtime hot path source 가 아니라 bootstrap fallback 참고 경로다.

## authoritative source

### 확정
- authoritative item template source: `NFX_GAME.game.item_template`
- canonical runtime cache: `ItemTemplateTable`
- canonical repository entrypoint: `ItemTemplateRepository`

### TODO
- `iVitality`, `iKi` 실제 공식 반영
- percent / refine / option / set bonus source 확장
- DB import/seed 자동화 (이번 티켓에서 item_template_import 도구 추가)

## load/init 경로

1. `WorldRuntime::OnRuntimeInit()`
2. `DatabaseInit()` 성공 후 `PreloadItemTemplateRepository_()` 호출
3. `ItemTemplateRepository::LoadCanonicalTableFromDb(...)`
4. 성공 시 `GetMutableCanonicalItemTemplateTable().Reset(...)`
5. 실패 또는 empty 시 `BootstrapCanonicalTableFromLegacyCsv()` 로 fallback bootstrap
6. 이후 runtime hot path 는 `ItemTemplateRepository::Find(item_id)` 만 사용

## provider / recompute 연결

- `EquipSummary`
- `ItemStatProvider::BuildItemStatBonus(...)`
- `ItemTemplateRepository::Find(item_id)`
- `RecomputeCombatStats(const CharacterCoreState&)`

즉 calculator 와 provider 는 더 이상 csv path 를 모른다.

## fallback 정책

### 운영 source
- 1순위: `game.item_template`
- preload 성공 시 info 로그 출력

### fallback source
- 2순위: legacy `005_00002.csv` bootstrap
- warn 로그로 명시
- canonical table 은 채우되, 운영 source 가 아니라 임시 bootstrap 임을 유지

### empty source
- DB preload 실패 + legacy bootstrap 실패
- recompute 는 기존 fallback bonus 를 사용
- warn 로그로 명시

## schema

정식 source 테이블:
- `NFX_GAME.game.item_template`

최소 컬럼:
- `item_id`
- `equip_part`
- `equip_tribe`
- `attack`
- `defense`
- `life`
- `mana`
- `vitality`
- `ki`
- `is_deleted`
- `source_tag`

## 운영 적용 전 수동 작업

1. `db/nfx_server_db_utf8_varchar_final.sql` 의 `game.item_template` migration 적용
2. [item_template_import_pipeline.md](G:\Programing\Work\new_server\docs\item_template_import_pipeline.md) 절차에 따라 초기 seed/import 수행
3. world server 시작 로그에서 preload count 와 fallback 여부 확인

## 아직 미반영된 항목

- `iVitality`, `iKi` 수식 반영
- 강화/IU/IS/퍼센트 옵션
- equip part 별 full aggregate
- repository reload/admin command

