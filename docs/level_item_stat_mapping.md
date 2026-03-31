# LEVEL / ITEM Stat Mapping

## 개요

이 문서는 `12sky1`의 `TS_ZONE` 기준 `LEVEL(005_00001.csv)` 과 `ITEM(005_00002.csv)` 데이터가 실제 어떤 stat source 로 쓰이는지 정리하고, `new_server`의 `RecomputeCombatStats(...)` 파이프라인에 이번 티켓에서 최소 반영한 범위를 기록한다.

핵심 원칙은 다음과 같다.

- `확정` 가능한 컬럼만 코드에 직접 반영한다.
- `추정` 컬럼은 문서에만 남기고 강한 공식으로 고정하지 않는다.
- `new_server`는 레거시 구조를 복붙하지 않고, `CharacterCoreState` 입력 품질만 높인다.

## 데이터설명.txt 근거

`G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA\데이터설명.txt`

- `005_00001.IMG/.csv = LEVEL`
- `005_00002.IMG/.csv = ITEM`
- `005_00003.IMG/.csv = SKILL`

즉 이번 티켓의 `LEVEL/ITEM` 매핑은 데이터설명.txt 기준으로도 확정 대상이다.

## 레거시 소스 근거 요약

| 근거 위치 | 판정 | 의미 |
| --- | --- | --- |
| `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S08_MyGameSystem.cpp:308-362` | 확정 | `LEVELSYSTEM::ReturnLevelFactorAttackPower/DefensePower/Life/Mana` 가 `mDATA[level-1].l*Info[tTribe]` 를 그대로 반환한다. |
| `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S06_MyGame02.cpp:137,223,425,473` | 확정 | `SetBasicAbilityFromEquip()` 및 관련 getter에서 `mLEVEL.ReturnLevelFactor*` 를 HP/MP/ATK/DEF 계산에 더한다. |
| `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S06_MyGame02.cpp:17-223` | 확정 | 장비 검색 후 `iVitality`, `iKi`, `iAttackPower`, `iDefensePower`, `iLife`, `iMana` 등을 합산한다. |
| `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S04_MyWork02.cpp:3997-4050` | 확정 | equip state packet 이 `aEquip[MAX_EQUIP_SLOT_NUM][3]` 형식으로 유지된다. |
| `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\H08_MyGameSystem.h:38-39,97-103` | 확정 | LEVEL/ITEM 시스템이 각각 `ReturnLevelFactor*`, `ITEMSYSTEM::Search/Return` 형태로 분리되어 있다. |

## 005_00001.csv 컬럼 매핑 표

| 컬럼 | 판정 | 의미 | 레거시 사용처 | new_server 반영 |
| --- | --- | --- | --- | --- |
| `lIndex` | 확정 | 레벨 번호(1..145) | `mDATA[level-1]` 인덱스 | `safe_level` lookup key |
| `lRangeInfo[0]` | 추정 | 해당 레벨 최소 경험치 | `ReturnLevelFactor1` 류 | 이번 티켓 미반영 |
| `lRangeInfo[1]` | 추정 | 해당 레벨 최대 경험치 | `ReturnLevelFactor2` 류 | 이번 티켓 미반영 |
| `lRangeInfo[2]` | 추정 | 스킬/능력치 관련 레벨 보조값 | `ReturnLevelFactor3` 류 | 이번 티켓 미반영 |
| `lRangeInfo[3]` | 추정 | 추가 progression factor | `ReturnLevelFactor4` 류 | 이번 티켓 미반영 |
| `lRangeInfo[4]` | 추정 | 추가 progression factor | `ReturnLevelFactor5` 류 | 이번 티켓 미반영 |
| `lAttackPowerInfo[0..2]` | 확정 | tribe 0/1/2의 레벨 공격 보정 | `ReturnLevelFactorAttackPower` | 반영 |
| `lDefensePowerInfo[0..2]` | 확정 | tribe 0/1/2의 레벨 방어 보정 | `ReturnLevelFactorDefensePower` | 반영 |
| `lAttackSuccessInfo[0..2]` | 확정 | 명중 관련 보정 | `ReturnLevelFactorAttackSuccess` | 이번 티켓 미반영 |
| `lAttackBlockInfo[0..2]` | 확정 | 회피/블록 관련 보정 | `ReturnLevelFactorAttackBlock` | 이번 티켓 미반영 |
| `lElementAttackInfo[0..2]` | 확정 | 속성 공격 보정 | `ReturnLevelFactorElementAttack` | 이번 티켓 미반영 |
| `lLifeInfo[0..2]` | 확정 | tribe 0/1/2의 레벨 HP 보정 | `ReturnLevelFactorLife` | 반영 |
| `lManaInfo[0..2]` | 확정 | tribe 0/1/2의 레벨 MP 보정 | `ReturnLevelFactorMana` | 반영 |

## 005_00002.csv 컬럼 매핑 표

| 컬럼 | 판정 | 의미 | 레거시 사용처 | new_server 반영 |
| --- | --- | --- | --- | --- |
| `iIndex` | 확정 | item template id | `ITEMSYSTEM::Search(iIndex)` | `item_id -> LegacyItemEntry` key |
| `iType` | 추정 | 아이템 대분류 | item usage/type branching | 이번 티켓 미반영 |
| `iSort` | 추정 | 세부 sort | part grouping/획득 테이블 | 이번 티켓 미반영 |
| `iLevel` | 추정 | item tier/추천 레벨 | `ITEMSYSTEM::Return(level, ...)` | 이번 티켓 미반영 |
| `iEquipTribe` | 확정 | tribe 착용 제한/분기 입력 | `Return(..., iTribe, ...)`, `ReturnForHigh(..., iEquipTribe, ...)` | 문서 반영, 코드 저장만 |
| `iEquipPart` | 확정 | equip slot/type | shared part table 및 equip slot 의미 | 문서 반영, 현재 summary 조립 참고 |
| `iVitality` | 확정 | vitality 기반 HP 간접 보정 | `SetBasicAbilityFromEquip()` | 이번 티켓 미반영 |
| `iKi` | 확정 | ki 기반 MP 간접 보정 | `SetBasicAbilityFromEquip()` | 이번 티켓 미반영 |
| `iAttackPower` | 확정 | 직접 공격 보정 | `mAttackPower += mEQUIP[index]->iAttackPower` | 반영 |
| `iDefensePower` | 확정 | 직접 방어 보정 | `mDefensePower += mEQUIP[index]->iDefensePower` | 반영 |
| `iLife` | 확정 | 직접 HP 보정 | `GetMaxLife()` 경로 | 반영 |
| `iMana` | 확정 | 직접 MP 보정 | `GetMaxMana()` 경로 | 반영 |
| `iAttackSucess` | 확정 | 명중 보정 | legacy only | 이번 티켓 미반영 |
| `iAttackBlock` | 확정 | 블록/회피 보정 | legacy only | 이번 티켓 미반영 |
| `iCritical` | 확정 | 치명타 보정 | legacy only | 이번 티켓 미반영 |
| `iLife_Up_Per` / `iMana_Up_Per` | 확정 | 퍼센트 보정 | legacy getter | 이번 티켓 미반영 |

## 해석 요약

### LEVEL
- `확정`: `005_00001.csv` 는 레벨별 성장표다.
- `확정`: 공격/방어/HP/MP 는 tribe 3종 배열로 나뉜다.
- `확정`: 레거시는 이 값을 직접 반환해 계산식에 더한다.
- `new_server` 반영: 이번 티켓에서 `RecomputeCombatStats(...)` 가 이 CSV를 lazy-load 해서 level scaling source 로 사용한다.

### ITEM
- `확정`: `005_00002.csv` 는 item template metadata 이다.
- `확정`: `iAttackPower/iDefensePower/iLife/iMana` 는 직접 stat bonus source 다.
- `확정`: `iVitality/iKi` 는 간접 stat source 다.
- `확정`: `iEquipPart/iEquipTribe` 는 장비 분류/착용 제한 source 다.
- `new_server` 반영: 이번 티켓에서는 `item_id -> direct atk/def/life/mana bonus` 까지만 연결하고, `iVitality/iKi` 및 강화/퍼센트/랜덤옵션은 미반영으로 둔다.

## new_server 반영 위치 표

| 위치 | 역할 |
| --- | --- |
| `G:\Programing\Work\new_server\src\services\world\common\character_combat_stat_calculator.cpp` | LEVEL/ITEM csv lazy-load + lookup + fallback |
| `G:\Programing\Work\new_server\src\services\world\db\world_character_repository.cpp` | `EquipSummary` 에 item template id 주입 |
| `G:\Programing\Work\new_server\src\services\world\runtime\world_runtime_enter_world.cpp` | enter 시 recompute 입력 로그 |
| `G:\Programing\Work\new_server\src\services\world\actors\player_actor.h` | recompute hook 재사용 |

## 이번 티켓 실제 반영 범위

1. `LEVEL` CSV를 직접 읽어 `max_hp/max_mp/atk/def` 의 level scaling source 로 사용한다.
2. `ITEM` CSV를 직접 읽어 `EquipSummary` 의 template id 를 `atk/def/hp/mp` bonus 로 해석한다.
3. CSV source 가 없으면 기존 hardcoded fallback 공식을 유지한다.
4. fallback 여부는 loader warn 로그로 드러난다.

## 이번 티켓 미반영 범위

| 항목 | 상태 | 이유 |
| --- | --- | --- |
| `iVitality/iKi` 를 HP/MP에 tribe multiplier 포함 방식으로 반영 | 미반영 | legacy는 tribe 계수와 결합해 계산하므로 추가 설계가 필요 |
| `iAttackSuccess/iAttackBlock/iCritical` 반영 | 미반영 | 현재 `new_server` combat runtime stat snapshot 범위 밖 |
| item 강화/IU/IS/퍼센트 옵션 | 미반영 | full item option system 범위이므로 제외 |
| skill/passive/element stat 반영 | 미반영 | 이번 티켓 범위 아님 |
| dedicated item template repository/db table | 미반영 | 현재는 legacy CSV lazy-load bridge 로 최소 연결 |
