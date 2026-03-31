# Portal / Zone Transition Test Plan

## 목적

- `zone_map_state(reason=enter_success | position_update | portal_moved | zone_changed)` 가 authoritative state 기준으로 내려오는지 확인
- DummyClient overlay auto-select 가 portal 이동 / zone 변경 이후에도 자동으로 따라가는지 확인
- `resources/zone_runtime.bin` 만으로 map/portal/NPC/monster overlay 와 테스트 경로가 재현 가능한지 확인

## 선행 조건

1. `zone_data_builder_d.exe` 실행으로 아래 산출물이 준비되어 있어야 한다.
   - `G:\Programing\Work\new_server\data_src\zone_csv\*.csv`
   - `G:\Programing\Work\new_server\resources\zone_runtime.bin`
2. `game.item_template` preload / fallback 관측성은 기존대로 유지되어야 한다.
3. 테스트용 계정/캐릭터가 최소 1개 이상 존재해야 한다.

## 기동 순서

1. `login_server_d.exe`
2. `account_server_d.exe`
3. `world_server_d.exe`
4. `zone_server_d.exe`
5. `DummyClientWinForms.exe`

## startup 검증 포인트

### world_server
- item template preload summary
- zone runtime binary load summary
  - `source`
  - `version`
  - `maps`
  - `portals`
  - `ready`

### zone_server
- zone runtime binary load summary
- `prewarmed_maps`
- `zone_id`

## authoritative source 정리

- enter success
  - `WorldRuntime::CompleteEnterWorldSuccessAfterZoneAck_()`
  - source: `CharacterCoreState.hot.position` + assigned zone/map
- position update
  - `WorldHandler::HandleWorldMove()`
  - source: `PlayerActor` accessor -> authoritative hot position
- portal moved
  - `WorldHandler::HandleWorldMove()` 에서 portal trigger hit 후 destination commit state
- zone changed
  - 같은 경로에서 `zone_id` 또는 `map_id` 가 바뀐 최종 state

## 최소 재현 경로

현재 코드 기준으로 확보된 최소 경로는 아래다.

1. 캐릭터 world enter
2. DummyClient 가 `enter_success` 수신
3. auto overlay zone 선택
4. 일반 이동으로 `position_update` 반복 확인
5. `portal_regions.csv` 에 정의된 portal center/radius 내부로 이동
6. world handler 가 portal trigger 를 감지하면 destination state commit
7. `portal_moved` 1회 송신
8. zone/map 값이 달라졌으면 `zone_changed` 추가 송신
9. DummyClient 가 새 zone 기준 overlay 를 자동 로드

## portal 후보 찾기

테스트 전 아래 csv 를 열어 실제 후보를 정한다.
- `data_src\zone_csv\portal_regions.csv`

추천 방법:
- 시작 캐릭터 zone 과 같은 `zone_id` 의 portal row 선택
- `center_x`, `center_z`, `radius` 근처로 이동
- `dest_zone_id`, `dest_map_id` 가 다른 row 를 우선 사용

## DummyClient 확인 항목

- 우측 상태 패널
  - `Zone/Map`
  - `Pos`
  - `ZoneMapReason`
- 중앙 2D 뷰
  - auto overlay on 상태인지
  - portal 이동 후 overlay 가 다른 zone 으로 바뀌는지
- manual fallback
  - `Auto overlay zone` 체크를 끄면 수동 zone 선택 가능해야 함

## 아직 비어 있는 부분 / TODO

- full zone-server authoritative portal system 은 아직 미완성
- builder 의 `dest_zone_id/value02` 해석은 최소 debug inference
- 일부 zone 에서는 legacy overlay 좌표계와 runtime `x/y` 가 완전히 1:1 이 아닐 수 있음
- map instance / portal destination 상세 packet 은 아직 없음

## 실패 시 체크 순서

1. `zone_runtime.bin` 존재 여부
2. world/zone startup binary load success 여부
3. DummyClient `DataRoot` 가 binary 경로를 가리키는지
4. `portal_regions.csv` 에 현재 zone portal row 가 실제 있는지
5. world log 에 `portal transition committed` 가 찍히는지
6. `zone_map_state` reason 이 `portal_moved` 또는 `zone_changed` 로 수신되는지
7. 그래도 안 되면 destination inference / runtime portal hook 미연결 여부를 확인
