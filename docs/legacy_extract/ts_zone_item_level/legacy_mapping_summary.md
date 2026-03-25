# TS_ZONE LEVEL / ITEM Legacy Extract Summary

## LEVEL 결론

- `확정`: `005_00001.csv` 는 LEVEL 성장표다.
- `확정`: `lAttackPowerInfo[0..2]`, `lDefensePowerInfo[0..2]`, `lLifeInfo[0..2]`, `lManaInfo[0..2]` 는 tribe 3종 성장 보정 컬럼이다.
- `확정`: 레거시 TS_ZONE 은 `LEVELSYSTEM::ReturnLevelFactor*` 로 이 값을 직접 반환해 전투 계산에 합산한다.
- `new_server` 반영: level table loader 로 lookup 하도록 연결했다.

## ITEM 결론

- `확정`: `005_00002.csv` 는 item template metadata 다.
- `확정`: `iIndex`, `iEquipPart`, `iEquipTribe`, `iAttackPower`, `iDefensePower`, `iLife`, `iMana`, `iVitality`, `iKi` 는 item template source 다.
- `확정`: 레거시 TS_ZONE 은 `SetBasicAbilityFromEquip()` 에서 item template row 를 찾아 direct/indirect stat bonus 를 합산한다.
- `new_server` 반영: canonical `ItemTemplateTable` 에 direct stat 계열만 우선 내재화했다.

## 확정 / 추정

### 확정
- LEVEL csv 의 attack/defense/life/mana tribe 성장 컬럼
- ITEM csv 의 equip part / equip tribe / direct stat bonus 컬럼
- `aTribe`, `aEquip` 가 zone runtime 으로 복사되는 흐름

### 추정
- legacy standalone `job/class` direct source
- ITEM csv 의 일부 분류 컬럼(`iType`, `iSort`, `iLevel`)의 정확한 gameplay 의미
- costume 전용 summary source

## 적용 범위

- canonical item template table 도입
- legacy csv loader / template table / item stat provider / calculator 분리
- legacy extract 산출물 생성

## 미적용 범위

- `iVitality`, `iKi` 직접 공식 반영
- 옵션/강화/퍼센트 bonus
- full item template repository/db 내재화
