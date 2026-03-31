# Dummy Client Zone/Map State

## 왜 필요한가

DummyClient 는 이미 `zone_map_state` packet 을 파싱할 수 있었지만, 이전에는 `enter_success` 와 일반 `position_update` 정도만 받는 상태라 portal 이동이나 zone/map transition 이후 overlay 자동 갱신이 완전하지 않았다. 이번 티켓에서는 같은 packet 을 `portal_moved`, `zone_changed` 까지 확장해서, live 좌표와 legacy overlay 를 같은 화면에서 더 자연스럽게 비교할 수 있게 했다.

## packet

- packet id: `proto::S2CMsg::zone_map_state` (`12`)
- struct: `proto::S2C_zone_map_state`

필드:
- `char_id`
- `zone_id`
- `map_id`
- `x`
- `y`
- `reason`
  - `enter_success`
  - `position_update`
  - `zone_changed`
  - `portal_moved`

## authoritative source

서버 기준 authoritative source:
- `CharacterCoreState.hot.position.zone_id`
- `CharacterCoreState.hot.position.map_id`
- `CharacterCoreState.hot.position.x`
- `CharacterCoreState.hot.position.y`
- `PlayerActor` projection 은 위 authoritative state 를 반영한 read-through/accessor 결과만 사용

reason 별 송신 근거:
- `enter_success`
  - `WorldRuntime::CompleteEnterWorldSuccessAfterZoneAck_()` 에서 enter 최종 성공 후 송신
- `position_update`
  - `WorldHandler::HandleWorldMove()` / `HandleWorldBenchMove()` 에서 authoritative move commit 후 송신
- `portal_moved`
  - `WorldHandler::HandleWorldMove()` 에서 portal trigger hit 후 destination position commit 직후 송신
- `zone_changed`
  - 같은 경로에서 `zone_id` 또는 `map_id` 가 실제로 바뀐 최종 상태에 대해 추가 송신

즉, 클라이언트는 더 이상 zone/map 을 추측하지 않고, 서버가 최종 확정한 값을 받는다.

## DummyClient auto-select

- 기본값: auto overlay on
- packet 수신 시:
  1. `DummyClientState.ZoneId/MapId/Pos/ZoneMapStateReason` 갱신
  2. auto mode 면 `ZoneSelector` 를 현재 zone 기준으로 맞춤
  3. `GameDataService` 가 binary overlay 를 다시 로드
- manual fallback:
  - `Auto overlay zone` 체크를 끄면 수동 zone selector 유지

## 수신 후 UI 동작

- 우측 상태 패널에 `Zone/Map`, `Pos`, `ZoneMapReason` 표시
- 중앙 2D 뷰는 현재 zone 기준 overlay 와 live player 좌표를 같이 렌더링
- portal 이동 후 `portal_moved`
- zone/map transition 후 `zone_changed`
  를 수신하면 overlay 가 자동으로 다시 맞춰진다.

## 후속 확장 지점

다음 티켓에서 같은 packet 을 재사용할 수 있는 위치:
- `OnWorldEnterSuccess -> SendZoneMapState(enter_success)`
- `OnPositionCommitted -> SendZoneMapState(position_update)`
- `OnPortalMoveCommitted -> SendZoneMapState(portal_moved)`
- `OnZoneOrMapTransitionCommitted -> SendZoneMapState(zone_changed)`

현재 portal / zone transition 은 world-side debug 경로 기준으로 최소 연결되어 있으며, full zone-server authoritative portal system 은 후속 확장 대상이다.

## 현재 한계

- `portal_moved` / `zone_changed` 는 현재 world handler 쪽 debug/runtime hook 중심이다.
- full portal gameplay, map instance reassignment, zone-server authoritative transition 전체가 완성된 것은 아니다.
- live 좌표는 `x/y`, legacy overlay 는 `mCENTER[0]/mCENTER[2]` 기반이라 zone 에 따라 완전히 1:1 일치하지 않을 수 있다.
