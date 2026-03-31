# Dummy Client WinForms

## 개요

- 경로: `G:\Programing\Work\new_server\tools\DummyClientWinForms`
- 목적: `login -> character list/select -> world enter -> stats -> move -> zone/map state -> object overlay` 를 빠르게 반복 검증하는 경량 WinForms 도구
- 범위: 검증 도구 전용. 실제 게임 UI 대체가 아님

## 구조

- `MainForm`: 전체 shell
- `Scenes/LoginPanel`: 서버 주소, 로그인 입력, 연결 상태
- `Scenes/CharacterPanel`: 캐릭터 목록, 선택, enter/stats/move/heal/gold/reconnect 액션
- `Scenes/WorldPanel`: 2D 디버그 렌더링, overlay zone 선택, object 선택 정보
- `Network/TcpClientEx`: async socket read/write
- `Network/PacketReader`, `PacketWriter`: 고정 길이 packet codec
- `Network/ClientProtocol`: 현재 `new_server` proto 재사용
- `Services/DummyClientState`: 계정/캐릭터/월드 상태
- `Services/GameDataService`: `resources/zone_runtime.bin` 로더
- `Services/WorldRenderService`: 2D GDI+ 렌더링

## 프로토콜 재사용 범위

- login: `1001/1101`
- character list: `1002/1102`
- character select: `1003/1103`
- world enter: `2001/2101`
- zone/map state: `12`
- stats: `10`
- move: `40`
- heal: `11`
- gold: `2`
- player AOI: `40/41/42/43/44/45`
- attack/spawn probe: `20/21`

## overlay 데이터 source

기존:
- DummyClient 가 legacy `TS_ZONE\DATA\*.WREGION` 을 직접 읽음

현재:
- `GameDataService` 는 `resources/zone_runtime.bin` 만 읽음
- env override: `DC_ZONE_RUNTIME_DATA_PATH`
- 기본 경로: `tools\DummyClientWinForms\bin\Debug\..\..\..\resources\zone_runtime.bin`

즉, DummyClient 는 runtime 과 동일 계열의 new_server 전용 binary asset 을 읽고, legacy 원본 포맷을 직접 읽지 않는다.

## 레거시 DATA 반영 방식

참고 원본:
- `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA`
- `데이터설명.txt`

이번 티켓에서 binary 로 보존한 영역:
- `Z###.WM` -> `maps.csv`
- `Z###_ZONEMOVEREGION.WREGION` -> `portal_regions.csv`
- `Z###_SUMMONNPC.WREGION`, `Z###_SUMMONGUARD*.WREGION` -> `summon_npc_regions.csv`
- `Z###_SUMMONMONSTER*.WREGION` -> `summon_monster_regions.csv`
- `Z###_ZONESAFEREGION.WREGION` -> `safe_regions.csv`
- `Z###_SPECIALREGION.WREGION` -> `special_regions.csv`

렌더링 규칙:
- `NPC`: 회색 원
- `Monster spawn`: 빨간 세모 + radius 원
- `Portal`: 금색 원
- `Player`: 파란 네모(자기 자신), 하늘색 네모(다른 플레이어)

좌표계:
- static overlay 는 `WORLD_REGION_INFO.mCENTER[0], mCENTER[2]` 를 2D 평면으로 사용
- live player 는 현재 `new_server` packet 의 `x/y`
- 따라서 화면은 “legacy region center 기반 static overlay + runtime live move point”를 겹쳐 보는 디버그 뷰다

## auto overlay

- 기본 동작: `zone_map_state` packet 수신 시 auto-select
- `reason` 표시: `enter_success / position_update / portal_moved / zone_changed`
- manual fallback: `Auto overlay zone` 체크 해제 후 수동 zone selector 사용
- world enter 직후와 일반 이동, portal 이동, zone/map transition 완료 후 현재 zone/map/pos 갱신

## 빌드

WinForms 빌드 예시:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' 'G:\Programing\Work\new_server\tools\DummyClientWinForms\DummyClientWinForms.csproj' /p:Configuration=Debug
```

실행 파일:
- `G:\Programing\Work\new_server\tools\DummyClientWinForms\bin\Debug\DummyClientWinForms.exe`

## 실행 순서

1. `login_server`, `account_server`, `world_server`, `zone_server` 기동
2. `zone_data_builder_d.exe` 로 `resources/zone_runtime.bin` 생성 확인
3. DummyClient 실행
4. host/port 입력 후 `Connect`
5. `Login`
6. `List`
7. 캐릭터 선택 후 `Select`
8. `Enter`
9. 필요 시 `Stats`, `Move`, `Heal`, `Gold+`, `Reconnect`
10. 중앙 2D 뷰에서 object 선택 및 overlay 확인

## preload/fallback 검증과 함께 보는 방법

1. world server startup 로그에서 item template preload summary 확인
2. world/zone server startup 로그에서 zone runtime binary load summary 확인
3. dummy client 로 enter
4. `Stats` 호출
5. world debug 로그에서 source summary / miss count / 최종 stat 확인
6. `zone_map_state` 수신 후 overlay zone 자동 선택 여부 확인
7. portal 이동/zone 변경 테스트 시 reason 값과 overlay 자동 갱신 확인

## 알려진 한계

- live object 실시간 반영은 현재 player AOI packet 중심
- monster/npc/portal 은 static overlay
- portal/zone transition 은 debug/test 경로 기준으로 최소 구현이며 full gameplay system 은 아님
- live `x/y` 와 legacy `X/Z` overlay 는 zone 에 따라 완전히 일치하지 않을 수 있음
