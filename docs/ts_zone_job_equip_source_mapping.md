# TS_ZONE Job/Equip Source Mapping

## 개요

이 문서는 `12sky1`의 `S07_TS_ZONE` 소스와 `DATA` 폴더를 기준으로 `job / tribe / equip summary / item template` source 를 정리하고, 이를 `new_server` 의 `CharacterCoreState -> ItemTemplateRepository -> ItemTemplateTable -> ItemStatProvider -> RecomputeCombatStats(...)` 경로에 어떻게 연결했는지 기록한다.

이번 티켓의 핵심은 calculator/provider 가 legacy csv 를 직접 읽지 않도록 `loader / repository / template table / provider / calculator` 를 분리하고, 운영 source 를 `game.item_template` 로 승격하는 것이다.

## 레거시 근거 요약

| 구분 | 근거 | 판정 | 설명 |
| --- | --- | --- | --- |
| tribe source | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S04_MyWork02.cpp:454` | 확정 | zone enter 시 `aTribe` 가 avatar runtime 으로 복사된다. |
| equip source | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S04_MyWork02.cpp:460` | 확정 | `aEquip` 전체가 zone runtime 으로 복사된다. |
| equip slot shape | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\H05_MyTransfer.h:70` | 확정 | `aEquip[MAX_EQUIP_SLOT_NUM][3]` 포맷이 유지된다. |
| level factor table | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA\005_00001.csv` | 확정 | tribe 3종 기준 `attack/defense/life/mana` 성장표다. |
| item template table | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA\005_00002.csv` | 확정 | `iIndex`, `iEquipPart`, `iEquipTribe`, `iAttackPower`, `iDefensePower`, `iLife`, `iMana`, `iVitality`, `iKi` 가 template source 다. |
| level runtime usage | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S08_MyGameSystem.cpp:308-362` | 확정 | `ReturnLevelFactorAttackPower/DefensePower/Life/Mana` 가 LEVEL row 값을 직접 반환한다. |
| equip runtime usage | `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S06_MyGame02.cpp:17-223` | 확정 | `SetBasicAbilityFromEquip()` 가 장착 item template stat 을 합산한다. |
| job/class direct field | TS_ZONE 범위 내 direct `aJob` 미확정 | 추정 | `new_server` 는 modernized `[game].[character].[job]` 를 authoritative source 로 유지한다. |

## CSV 매핑 요약

### LEVEL

| source | target | 판정 | 비고 |
| --- | --- | --- | --- |
| `lIndex` | level lookup key | 확정 | `LegacyLevelTable::Find(level)` |
| `lAttackPowerInfo[0..2]` | level attack scaling | 확정 | tribe별 attack bonus |
| `lDefensePowerInfo[0..2]` | level defense scaling | 확정 | tribe별 defense bonus |
| `lLifeInfo[0..2]` | level hp scaling | 확정 | tribe별 max hp bonus |
| `lManaInfo[0..2]` | level mp scaling | 확정 | tribe별 max mp bonus |

### ITEM

| source | target | 판정 | 비고 |
| --- | --- | --- | --- |
| `iIndex` | `ItemTemplate.item_id` | 확정 | template lookup key |
| `iEquipPart` | `ItemTemplate.equip_part` | 확정 | weapon/armor/accessory 분류 입력 |
| `iEquipTribe` | `ItemTemplate.equip_tribe` | 확정 | 파싱/향후 validation 용 |
| `iAttackPower` | `ItemTemplate.attack` | 확정 | direct bonus 반영 |
| `iDefensePower` | `ItemTemplate.defense` | 확정 | direct bonus 반영 |
| `iLife` | `ItemTemplate.life` | 확정 | direct bonus 반영 |
| `iMana` | `ItemTemplate.mana` | 확정 | direct bonus 반영 |
| `iVitality` | `ItemTemplate.vitality` | 확정 | 이번 티켓은 파싱만 |
| `iKi` | `ItemTemplate.ki` | 확정 | 이번 티켓은 파싱만 |

## new_server 매핑표

| legacy/data source | new_server target | 이번 반영 | 비고 |
| --- | --- | --- | --- |
| `aLevel` + `LEVEL csv` | `CharacterIdentity.level` + `LegacyLevelTable` | 반영 | calculator direct csv load 제거 |
| `aTribe` | `CharacterIdentity.tribe` | 반영 | snapshot repository query source |
| modernized DB `job` | `CharacterIdentity.job` | 반영 | legacy job direct source 는 미확정 |
| `aEquip[0]` | `EquipSummary.weapon_template_id` | 반영 | 무기 슬롯 |
| `aEquip[1..5]` | `EquipSummary.armor_template_id` | 반영 | 대표 armor template |
| `aEquip[6..7]` | `EquipSummary.accessory_template_id` | 반영 | 대표 accessory template |
| `game.item_template` row | `ItemTemplateRepository` / `ItemTemplateTable` | 반영 | 운영 canonical source |
| `ITEM csv row` | legacy bootstrap fallback | 반영 | preload 실패 시에만 사용 |
| `equip summary + template table` | `ItemStatProvider` | 반영 | direct stat bonus 집계 |
| `CharacterCoreState` | `RecomputeCombatStats(...)` 입력 | 반영 | authoritative source 유지 |

## 이번 티켓에서 실제 반영한 범위

1. `legacy_level_csv_loader.*` 로 LEVEL lookup 을 분리했다.
2. `legacy_item_csv_loader.*` 로 ITEM csv 파싱을 분리했다.
3. `item_template_table.*` 로 canonical item template table 을 도입했다.
4. `item_stat_provider.*` 로 equip summary -> direct stat bonus 집계를 분리했다.
5. `character_combat_stat_calculator.cpp` 는 이제 provider/table 을 사용하고, template miss 시에만 fallback 을 사용한다.
6. `docs/legacy_extract/ts_zone_item_level/` 아래에 legacy extract 산출물을 남겼다.

## 아직 미반영 범위

| 항목 | 상태 | 이유 |
| --- | --- | --- |
| `iVitality`, `iKi` 공식 반영 | 미반영 | tribe/derived formula 정리가 더 필요 |
| 강화/IU/IS/퍼센트 옵션 | 미반영 | full option pipeline 범위 |
| costume source wiring | 미반영 | 현재 DB/source 부족 |
| dedicated internal item template DB/table | 미반영 | 현재는 legacy csv loader 기반 canonical table |

## 관련 산출물

- [legacy_mapping_summary.md](G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\legacy_mapping_summary.md)
- [level_column_mapping.csv](G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\level_column_mapping.csv)
- [item_column_mapping.csv](G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\item_column_mapping.csv)
- [source_reference.csv](G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\source_reference.csv)
- [db_schema_reference.csv](G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\db_schema_reference.csv)

