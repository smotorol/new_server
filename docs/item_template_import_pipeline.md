# Item Template Import Pipeline

## 개요

이 문서는 legacy `ITEM(005_00002.csv)` 또는 repo 내부 extract 산출물을 `NFX_GAME.game.item_template` 로 적재하는 운영용 import 절차를 정리한다.

runtime authoritative source 는 DB preload 결과이고, legacy csv 는 import 입력 source 또는 비상 fallback 참고 source다.

## 입력 source

우선순위:
1. `G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\original_item.csv`
2. `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA\005_00002.csv`

override:
- `--source <csv-path>`

## source column -> DB column 매핑

| source column | DB column | 판정 | 비고 |
| --- | --- | --- | --- |
| `iIndex` | `item_id` | 확정 | key |
| `iEquipPart` | `equip_part` | 확정 | direct mapping |
| `iEquipTribe` | `equip_tribe` | 확정 | direct mapping |
| `iAttackPower` | `attack` | 확정 | direct mapping |
| `iDefensePower` | `defense` | 확정 | direct mapping |
| `iLife` | `life` | 확정 | direct mapping |
| `iMana` | `mana` | 확정 | direct mapping |
| `iVitality` | `vitality` | 확정 | 이번 티켓은 저장만 |
| `iKi` | `ki` | 확정 | 이번 티켓은 저장만 |
| 없음 | `is_deleted` | 확정 | import 시 0 |
| 없음 | `source_tag` | 확정 | 기본 `legacy_csv_import` |

## 실행 방법

예시:

```powershell
item_template_import --conn "DSN=MyGame;UID=sa;PWD=***;DATABASE=NFX_GAME;" --dry-run
item_template_import --conn "DSN=MyGame;UID=sa;PWD=***;DATABASE=NFX_GAME;" --source "G:\Programing\Work\new_server\docs\legacy_extract\ts_zone_item_level\original_item.csv"
```

## upsert 정책

- key: `item_id`
- item_id가 없으면 `insert`
- item_id가 있고 값이 달라지면 `update`
- item_id가 있고 값이 같으면 `skip`
- invalid row는 `skip + invalid count`
- duplicate source row는 `skip + duplicate count` (first row wins)

## transaction 정책

선택: 전체 import 1 transaction

이유:
- seed/update 결과를 한 번에 롤백 가능
- 재실행 시 idempotent 결과 보장 용이
- dry-run 구현이 단순함

정책:
- `BEGIN TRANSACTION`
- 전체 row upsert
- `--dry-run` 이면 `ROLLBACK`
- 아니면 `COMMIT`
- DB 예외면 프로세스 실패로 종료하고 트랜잭션은 연결 종료 기준 rollback 전제

## 결과 출력

출력/로그 항목:
- source file path
- inserted
- updated
- skipped
- invalid
- duplicates
- live row count

## preload 검증 방법

import 후 world server 에서 확인할 것:
1. preload count > 0
2. `Item template preload ready` 로그 확인
3. `fallback bootstrap` warn 미발생 또는 감소
4. equip 있는 캐릭터의 item bonus 가 repository 경유로 반영
5. template miss warn 감소 여부 확인

## fallback 해소 절차

1. migration 으로 `game.item_template` 생성
2. import 도구로 seed/update 수행
3. world server preload 확인
4. fallback warn 감소 확인
5. 운영 안정화 후 legacy csv bootstrap 제거 검토

## TODO

- `iVitality`, `iKi` 공식 반영
- 옵션/강화/퍼센트 bonus import 확장
- import 결과 리포트 파일 저장
- admin/reload command
