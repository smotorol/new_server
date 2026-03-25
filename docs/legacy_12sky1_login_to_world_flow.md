# Legacy 12Sky1 Login To World Flow

## 개요

- 목적: `12sky1`에서 클라이언트 로그인부터 캐릭터 선택, 월드/존 진입, 최종 플레이 가능 상태까지의 실제 흐름을 추적하고, 이를 `new_server`의 `login/account/world/zone` 구조에 어떻게 대응시킬지 설계 기준으로 정리한다.
- 집중 분석 대상:
  - `G:\Programing\Work\12sky1\12sky1\S06_TS_LOGIN`
  - `G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE`
- 보조 참조:
  - `G:\Programing\Work\12sky1\12sky1\S01_TS_PLAYUSER`
  - `G:\Programing\Work\12sky1\12sky1\S02_TS_CENTER`
- 신규 대응 대상:
  - `G:\Programing\Work\new_server\src\services\login`
  - `G:\Programing\Work\new_server\src\services\account`
  - `G:\Programing\Work\new_server\src\services\world`
  - `G:\Programing\Work\new_server\src\services\zone`
  - `G:\Programing\Work\new_server\src\proto\client`
  - `G:\Programing\Work\new_server\src\proto\internal`
  - `G:\Programing\Work\new_server\src\server_common`
- 핵심 결론:
  - 레거시는 `LOGIN`이 인증과 캐릭터 로딩 및 zone endpoint 반환까지 담당한다.
  - 실제 "플레이 가능 상태" 진입은 `ZONE`의 `W_TEMP_REGISTER_SEND -> W_REGISTER_AVATAR_SEND` 단계에서 완료된다.
  - `PLAYUSER`는 로그인 성공 후 캐릭터/존 상태를 기억하는 세션 레지스트리 역할을 한다.
  - `CENTER`는 로그인/존 양쪽에 zone endpoint를 알려 주는 서버 디렉터리이자 월드 공용 상태 분배자다.
  - `new_server`는 같은 흐름을 유지하되, `login -> account -> world -> zone`으로 책임을 분해하고 `login_session/world_token`을 중심으로 결합을 끊어야 한다.

## 레거시 실제 로그인→입장 흐름

### 단계별 시퀀스

#### 레거시 실제 시퀀스

1. `Client -> S06_TS_LOGIN`
   - 로그인 요청 패킷이 `W_DEMAND_LOGIN`으로 들어온다.
   - 근거: `S06_TS_LOGIN/S04_MyWork01.cpp`, `S06_TS_LOGIN/S04_MyWork02.cpp`
   - 관여 클래스: `MyServer`, `MyWork`, `MyUser`, `MyDB`

2. `S06_TS_LOGIN`
   - 버전, 접속 제한, 차단 IP, 최대 인원, 서버 버전 등을 먼저 검사한다.
   - 근거: `W_DEMAND_LOGIN` 내부의 `CompareBlockIPInfo`, `mMaxPlayerNum`, `mPresentPlayerNum`, `mServerVersion`
   - 판정: `확정`

3. `S06_TS_LOGIN -> DB`
   - `MyDB::DB_LOGIN`이 계정 인증, block 상태, 로그인 가능 여부, 부가 보안 정보(비밀카드/마우스패스워드)를 확인한다.
   - 근거: `S06_TS_LOGIN/S06_MyDB.cpp`, `DB_LOGIN`
   - 판정: `확정`

4. `S06_TS_LOGIN -> Client`
   - 로그인 성공 시 `B_DEMAND_LOGIN_RESULT(0, ...)`를 전송한다.
   - 응답에는 `userSort`, 보조 인증 인덱스, `uID`, 급여 플래그, 2차 로그인 상태, 마우스 패스워드 표시 정보가 들어간다.
   - 근거: `S06_TS_LOGIN/S05_MyTransfer.cpp`, `B_DEMAND_LOGIN_RESULT`
   - 판정: `확정`

5. `Client -> S06_TS_LOGIN`
   - 캐릭터 목록 요청이 `W_DEMAND_USER_AVATAR`로 들어온다.
   - 근거: `S06_TS_LOGIN/S04_MyWork02.cpp`
   - 판정: `확정`

6. `S06_TS_LOGIN -> DB`
   - `DB_LOAD_AVATAR`가 캐릭터 슬롯별 `AVATAR_INFO`를 로드한다.
   - 이후 길드 정보 정합성 보정, 이벤트 아이템 정리, 저장함/아이템/장비/포션 정리까지 로그인 서버에서 수행한다.
   - 근거: `S06_TS_LOGIN/S04_MyWork02.cpp`, `S06_TS_LOGIN/S06_MyDB.cpp`
   - 판정: `확정`

7. `S06_TS_LOGIN -> Client`
   - `B_DEMAND_USER_AVATAR_RESULT(0)` 뒤에 슬롯별 `B_USER_AVATAR_INFO`를 반복 전송한다.
   - 즉, 캐릭터 목록은 단일 응답이 아니라 "목록 시작" + "슬롯별 아바타 정보" 형태다.
   - 근거: `S06_TS_LOGIN/S04_MyWork02.cpp`, `S06_TS_LOGIN/S05_MyTransfer.cpp`
   - 판정: `확정`

8. `Client -> S06_TS_LOGIN`
   - 사용자가 zone/world를 고르고 캐릭터 슬롯을 선택하면 `W_DEMAND_ZONE_SERVER_INFO_1`가 호출된다.
   - 입력 데이터는 `tZoneNumber`, `tAvatarPost`다.
   - 근거: `S06_TS_LOGIN/S04_MyWork02.cpp`
   - 판정: `확정`

9. `S06_TS_LOGIN -> S02_TS_CENTER`
   - `mCENTER_COM.U_GET_ZONE_SERVER_INFO_1(uID, tZoneNumber)`로 선택 zone의 IP/port를 얻는다.
   - `CENTER`는 zone server registry를 보고 결과를 응답한다.
   - 근거:
     - `S06_TS_LOGIN/S04_MyWork02.cpp`
     - `S02_TS_CENTER/S04_MyWork02.cpp`, `W_GET_ZONE_SERVER_INFO_1`
   - 판정: `확정`

10. `S06_TS_LOGIN -> S01_TS_PLAYUSER`
   - zone endpoint 확보 후 `mPLAYUSER_COM.U_REGISTER_USER_FOR_LOGIN_3_SEND(id, avatarInfo)`를 호출해 "이 계정이 이제 특정 아바타를 들고 zone 진입 대기 상태"임을 등록한다.
   - 근거:
     - `S06_TS_LOGIN/S04_MyWork02.cpp`
     - `S01_TS_PLAYUSER/S07_MyGame01.cpp`, `RegisterUserForLogin_3`
   - 판정: `확정`

11. `S06_TS_LOGIN -> Client`
   - `B_DEMAND_ZONE_SERVER_INFO_1_RESULT(result, ip, port)`를 전송한다.
   - 이 시점까지는 아직 zone 접속 전이며, `mEnterZoneResult = 1`은 "진입 진행 중" 플래그에 가깝다.
   - 근거:
     - `S06_TS_LOGIN/S04_MyWork02.cpp`
     - `S06_TS_LOGIN/S05_MyTransfer.cpp`
   - 판정: `확정`

12. `Client -> S07_TS_ZONE`
   - client는 login이 알려준 zone endpoint로 새 접속을 열고, 먼저 `W_TEMP_REGISTER_SEND`를 보낸다.
   - packet에는 최소 `id`, `tribe`가 포함된다.
   - 근거: `S07_TS_ZONE/S04_MyWork02.cpp`
   - 판정: `확정`

13. `S07_TS_ZONE`
   - `W_TEMP_REGISTER_SEND`에서 임시 입장 가능 여부를 확인한다.
   - 중복 임시 등록, tribe 수용 제한, 같은 ID의 중복 temp register를 검사한다.
   - 성공하면 `mCheckTempRegister = TRUE`, `mTempRegisterTribe = tribe`, `mID = id`.
   - 근거:
     - `S07_TS_ZONE/H03_MyUser.h`
     - `S07_TS_ZONE/S04_MyWork02.cpp`
   - 판정: `확정`

14. `Client -> S07_TS_ZONE`
   - 이어서 `W_REGISTER_AVATAR_SEND`를 보낸다.
   - packet에는 `id`, `avatarName`, 일부 action/location 정보가 들어간다.
   - 근거: `S07_TS_ZONE/S04_MyWork02.cpp`
   - 판정: `확정`

15. `S07_TS_ZONE -> S01_TS_PLAYUSER`
   - `mPLAYUSER_COM.U_REGISTER_USER_FOR_ZONE_1_SEND(id, avatarName, logoutInfo)`를 호출한다.
   - `PLAYUSER`는 로그인 서버가 미리 `REGISTER_USER_FOR_LOGIN_3`으로 저장해 둔 `AVATAR_INFO`를 찾아 zone 진입 1단계를 승인한다.
   - 근거:
     - `S07_TS_ZONE/S04_MyWork02.cpp`
     - `S01_TS_PLAYUSER/S07_MyGame01.cpp`, `RegisterUserForZone_1`
   - 판정: `확정`

16. `S07_TS_ZONE -> S02_TS_CENTER`
   - 필요 시 `U_DOUBLE_CONNECT_USER_SEND`가 호출되어 기존 접속 zone에 중복 접속 정리 신호를 보낸다.
   - 근거:
     - `S07_TS_ZONE/S04_MyWork02.cpp`
     - `S02_TS_CENTER/S04_MyWork02.cpp`, `W_DOUBLE_CONNECT_USER_SEND`
   - 판정: `확정`

17. `S07_TS_ZONE`
   - `PLAYUSER`가 돌려준 `AVATAR_INFO`, 효과값, 유저 sort, trace, PCBang 정보 등을 사용해 `mUSER[tUserIndex]`와 `mAVATAR_OBJECT[tUserIndex]`를 초기화한다.
   - 이후 월드 시간, 아바타 액션, 세션 체크, 길드/파티/영웅/캐시 정보 등을 클라이언트에 보낸다.
   - 근거: `S07_TS_ZONE/S04_MyWork02.cpp`
   - 판정: `확정`

18. `S07_TS_ZONE -> Client`
   - `B_REGISTER_AVATAR_RECV(0, &tAvatarInfo)` 후 여러 초기 상태 패킷을 보내고, 마지막에 `B_REGISTER_AVATAR_POST_RECV()`를 보낸다.
   - 이 구간이 레거시에서 사실상 "플레이 가능한 상태" 완료 지점이다.
   - 근거:
     - `S07_TS_ZONE/H05_MyTransfer.h`
     - `S07_TS_ZONE/S04_MyWork02.cpp`
   - 판정: `확정`

19. `S07_TS_ZONE -> S01_TS_PLAYUSER`
   - 로그인 또는 존 이동 완료 후 `U_REGISTER_USER_FOR_ZONE_2_SEND`로 최종 상태를 저장한다.
   - `PLAYUSER`는 이를 받아 zone 번호, 효과값, 아바타 상태를 최종 반영한다.
   - 근거:
     - `S07_TS_ZONE/S03_MyUser.cpp`
     - `S07_TS_ZONE/S10_MyUpperCom.cpp`
     - `S01_TS_PLAYUSER/S07_MyGame01.cpp`, `RegisterUserForZone_2`
   - 판정: `확정`

### 텍스트 시퀀스 다이어그램

```text
1. Client -> LOGIN : login_request(id, password, version, webmode)
2. LOGIN -> DB : DB_LOGIN(...)
3. LOGIN -> Client : DEMAND_LOGIN_RESULT(ok / fail)
4. Client -> LOGIN : demand_user_avatar
5. LOGIN -> DB : DB_LOAD_AVATAR(...)
6. LOGIN -> Client : DEMAND_USER_AVATAR_RESULT + USER_AVATAR_INFO*
7. Client -> LOGIN : demand_zone_server_info_1(zoneNumber, avatarPost)
8. LOGIN -> CENTER : GET_ZONE_SERVER_INFO_1(id, zoneNumber)
9. CENTER -> LOGIN : zone ip/port
10. LOGIN -> PLAYUSER : REGISTER_USER_FOR_LOGIN_3(id, avatarInfo)
11. LOGIN -> Client : zone ip/port
12. Client -> ZONE : TEMP_REGISTER_SEND(id, tribe)
13. ZONE -> Client : TEMP_REGISTER_RECV
14. Client -> ZONE : REGISTER_AVATAR_SEND(id, avatarName, action)
15. ZONE -> PLAYUSER : REGISTER_USER_FOR_ZONE_1(id, avatarName, logoutInfo)
16. PLAYUSER -> ZONE : avatarInfo + effect + userSort + duplicate state
17. ZONE -> CENTER : optional double-connect notify / world info sync
18. ZONE -> Client : REGISTER_AVATAR_RECV + world time + avatar action + session check + post recv
19. ZONE -> PLAYUSER : REGISTER_USER_FOR_ZONE_2(final avatar/effect state)
20. Client : now playable
```

## 단계별 함수/클래스/패킷 근거

### 1. 로그인 요청 수신 지점

| 질문 | 답 | 근거 | 판정 |
| --- | --- | --- | --- |
| 로그인 요청 패킷은 어디서 수신되는가 | `S06_TS_LOGIN`의 `W_DEMAND_LOGIN` | `S06_TS_LOGIN/S04_MyWork01.cpp`, `S06_TS_LOGIN/S04_MyWork02.cpp` | 확정 |
| 어떤 클래스들이 관여하는가 | `MyServer -> MyWork -> MyUser -> MyDB` | `H01_MainApplication.h`, `H03_MyUser.h`, `H04_MyWork.h`, `H06_MyDB.h` | 확정 |

### 2. 계정 인증 처리 지점

| 질문 | 답 | 근거 | 판정 |
| --- | --- | --- | --- |
| 계정 인증은 어디서 처리되는가 | `MyDB::DB_LOGIN` | `S06_TS_LOGIN/S06_MyDB.cpp` | 확정 |
| 인증 source of truth는 무엇인가 | DB + 외부 포털 인증(`PortalLogin`, `PortalWebLoginCheck`) | `DB_LOGIN` 내부 | 확정 |
| 로그인 상태/차단/보안도 여기서 보는가 | 그렇다 | `uBlockSort`, `uBlockInfo`, `uMousePassword`, `uCheckSalary`, `uCheckTrace` 조회 | 확정 |

### 3. 캐릭터 목록 / 캐릭터 선택 / 서버 선택

| 질문 | 답 | 근거 | 판정 |
| --- | --- | --- | --- |
| 캐릭터 목록은 어디서 로딩되는가 | `W_DEMAND_USER_AVATAR -> DB_LOAD_AVATAR` | `S06_TS_LOGIN/S04_MyWork02.cpp`, `S06_TS_LOGIN/S06_MyDB.cpp` | 확정 |
| 캐릭터 정보는 어떻게 응답되는가 | `B_DEMAND_USER_AVATAR_RESULT` 후 슬롯별 `B_USER_AVATAR_INFO` 반복 | `S06_TS_LOGIN/S05_MyTransfer.cpp` | 확정 |
| 캐릭터 선택과 zone 선택은 어디서 합쳐지는가 | `W_DEMAND_ZONE_SERVER_INFO_1(zoneNumber, avatarPost)` | `S06_TS_LOGIN/S04_MyWork02.cpp` | 확정 |
| zone endpoint는 누가 결정하는가 | `CENTER` | `S02_TS_CENTER/S04_MyWork02.cpp`, `W_GET_ZONE_SERVER_INFO_1` | 확정 |

### 4. zone 입장 완료 지점

| 질문 | 답 | 근거 | 판정 |
| --- | --- | --- | --- |
| 실제 입장 완료는 어디인가 | `S07_TS_ZONE/W_REGISTER_AVATAR_SEND`의 후반부 | `S07_TS_ZONE/S04_MyWork02.cpp` | 확정 |
| 플레이 가능 상태를 알리는 신호는 무엇인가 | `B_REGISTER_AVATAR_POST_RECV()` | `S07_TS_ZONE/H05_MyTransfer.h`, `S07_TS_ZONE/S04_MyWork02.cpp` | 확정 |
| 레거시에서 world/zone 경계는 어떤가 | 거의 zone 하나가 world/gameplay 진입을 모두 처리 | `ZONE`가 월드 시간/아바타 상태/길드/파티/캐시/이벤트까지 모두 전송 | 확정 |

### 5. 중간 조정 서버의 역할

| 서버 | 역할 | 근거 | 판정 |
| --- | --- | --- | --- |
| `PLAYUSER` | 로그인 후 선택된 아바타와 zone 상태를 기억하는 세션 레지스트리 | `RegisterUserForLogin_1/2/3`, `RegisterUserForZone_1/2`, `UnRegisterUser` | 확정 |
| `CENTER` | zone directory + 월드 공용 상태 + 중복 접속 통지 허브 | `W_GET_ZONE_SERVER_INFO_1/2`, `W_DOUBLE_CONNECT_USER_SEND` | 확정 |

## 실패 분기

### 레거시에서 확인된 실패

| 실패 유형 | 레거시 위치 | 세부 내용 | 판정 |
| --- | --- | --- | --- |
| 인증 실패 | `W_DEMAND_LOGIN` | `DB_LOGIN` 결과 코드 기반 실패 응답 | 확정 |
| 차단 IP | `W_DEMAND_LOGIN` | `CompareBlockIPInfo`면 결과 코드 `98` | 확정 |
| 서버 닫힘 / 인원 초과 | `W_DEMAND_LOGIN` | `mMaxPlayerNum == 0`, `mPresentPlayerNum >= mMaxPlayerNum` | 확정 |
| 버전 불일치 | `W_DEMAND_LOGIN` | 서버 버전 mismatch 시 코드 `7` | 확정 |
| 캐릭터 없음 / 로드 실패 | `W_DEMAND_USER_AVATAR` | `DB_LOAD_AVATAR` 결과 코드 전파 | 확정 |
| 잘못된 캐릭터 슬롯 | `W_DEMAND_ZONE_SERVER_INFO_1` | invalid `tAvatarPost`면 세션 종료 | 확정 |
| zone server 없음 / 비정상 | `CENTER::W_GET_ZONE_SERVER_INFO_1` | 없으면 `0.0.0.0:0`와 failure code 반환 | 확정 |
| login side zone enter fail rollback | `W_FAIL_MOVE_ZONE_1_SEND` | login의 `mEnterZoneResult`를 되돌림 | 확정 |
| temp register 실패 | `ZONE::W_TEMP_REGISTER_SEND` | tribe 수용 제한, temp 중복, ID 중복 | 확정 |
| 중복 접속 | `PLAYUSER::RegisterUserForZone_1`, `CENTER::W_DOUBLE_CONNECT_USER_SEND` | `__DOUBLE_CONNECT__` 빌드에서 기존 zone 추적 및 끊기 | 확정 |
| zone 입장 규칙 실패 | `ZONE::W_REGISTER_AVATAR_SEND` | wrong zone/level/kill count 등에서 세션 종료 | 확정 |
| 이동 존 실패 후 롤백 | `ZONE::W_FAIL_MOVE_ZONE_2_SEND` | 이동 실패 처리용 후속 단계 존재 | 확정 |

### 아직 추정인 지점

| 항목 | 추정 내용 | 이유 |
| --- | --- | --- |
| 로그인 성공 직후 `PLAYUSER` 등록 1/2 단계의 호출 위치 | `S06_TS_LOGIN` 내부 다른 경로에서 `REGISTER_USER_FOR_LOGIN_1/2`가 먼저 수행될 가능성 | 현재 수집한 흐름에서 `REGISTER_USER_FOR_LOGIN_3`는 확인됐지만 1/2의 정확한 호출 지점은 추가 추적 필요 |
| client가 `zoneNumber`를 어떻게 선택하는지 | login 서버가 캐릭터 목록과 별개로 서버 목록/zone 목록을 별도 제공했을 가능성 | 이번 분석 범위에서 해당 UI/패킷은 직접 확인하지 못함 |

## new_server 대응 설계안

### 설계 원칙

1. 인증 source of truth는 `account_server`가 가져야 한다.
2. `login/account/world/zone` 경계를 흐리지 않는다.
3. `proto/client`와 `proto/internal` 분리를 유지한다.
4. `world token` / `login_session` 기반 구조를 유지한다.
5. 중복 로그인, stale close, reconnect 정책은 `login/world`의 세션 state machine과 충돌하지 않아야 한다.

### 권장 신규 시퀀스

```text
1. Client -> login_server
   C2S_login_request(login_id, password, selected_char_id)

2. login_server -> account_server
   internal::login_account::AccountAuthRequest

3. account_server
   - DB 인증
   - selected_char_id 검증
   - selectable world route 선택
   - login_session / world_token 발급

4. account_server -> login_server
   internal::login_account::AccountAuthResult

5. login_server -> Client
   proto::login::S2C_login_result(account_id, char_id, login_session, world_host, world_port, world_token)

6. Client -> world_server
   proto::world::C2S_enter_world_with_token(account_id, char_id, login_session, world_token)

7. world_server -> account_server
   internal::account_world::WorldAuthTicketConsumeRequest

8. account_server -> world_server
   internal::account_world::WorldAuthTicketConsumeResponse

9. world_server
   - authenticated world session bind
   - duplicate login resolution
   - enter-world pending state 시작
   - zone route 선택 / map assign 요청

10. world_server -> zone_server
    internal::world_zone::WorldZoneMapAssignRequest

11. zone_server -> world_server
    ZoneWorldMapAssignResponse(ok)

12. world_server -> zone_server
    WorldZonePlayerEnter

13. zone_server -> world_server
    ZoneWorldPlayerEnterAck(ok)

14. world_server -> account_server
    WorldEnterSuccessNotify

15. account_server -> login_server
    WorldEnterSuccessNotify relay

16. world_server -> Client
    S2C_enter_world_result(success)

17. login_server
    login_session 정리
```

### 신규 모듈 대응

| 레거시 단계 | 신규 모듈 | 근거 |
| --- | --- | --- |
| `W_DEMAND_LOGIN` | `src/services/login/handler/login_handler.cpp` | `IssueLoginRequest` 호출 |
| `DB_LOGIN` + 캐릭터 선택 검증 | `src/services/account/runtime/account_line_runtime.cpp`, `src/services/account/db/*` | `HandleDqsResult_`, `AccountAuthDbRepository` |
| zone endpoint 선택 | `AccountLineRuntime::TrySelectWorldRouteEndpoint_` | world route registry 기반 |
| `LOGIN -> Client` zone ip/port 반환 | `proto/client/login_proto.h` | `S2C_login_result`가 `world_host/world_port/world_token` 포함 |
| `ZONE temp register + avatar register` | `world_server`의 `enter_world_with_token` + `zone_server`의 `OnMapAssignRequest_`, `OnPlayerEnterRequest_` | 레거시 2단계를 token consume + zone ack 두 단계로 분해 |
| `PLAYUSER` 상태 등록 | `world_server` 세션/registry + 필요 시 cache | 별도 process 없이 `authed_sessions_by_sid_`, pending enter state로 대체 |
| `CENTER` directory 기능 | `account_server` world route registry | `RegisteredWorldEndpoint`, route heartbeat |

## 현재 new_server와의 차이점

### 잘 맞는 점

| 항목 | 현재 new_server 구현 | 의미 |
| --- | --- | --- |
| login/account 분리 | `login_handler -> LoginLineRuntime -> account line` | 레거시 로그인 서버 과부하를 분산 |
| token 기반 진입 | `login_session`, `world_token` | 레거시의 ip/port 직접 인계보다 안전 |
| account가 world route 선택 | `AccountLineRuntime::TrySelectWorldRouteEndpoint_` | `CENTER`의 directory 기능을 현대화 |
| world가 최종 세션 바인딩 보유 | `TryBeginEnterWorldSession`, `BindAuthenticatedWorldSessionForLogin` | "누가 최종 플레이 세션의 owner인가"가 명확 |
| zone이 map/instance/player binding 담당 | `ZoneRuntime::OnMapAssignRequest_`, `OnPlayerEnterRequest_` | 레거시 zone giant logic 중 route 부분이 분리됨 |
| duplicate login 처리 | `LoginLineRuntime`, `WorldRuntime` 모두 명시적 처리 | 레거시보다 state machine이 선명함 |

### 아직 레거시와 다른 점

| 항목 | 레거시 | new_server 현재 | 판단 |
| --- | --- | --- | --- |
| 캐릭터 목록 조회 단계 | login 서버가 직접 avatar list 제공 | 현재 `login_request`에 `selected_char_id`가 이미 들어옴 | 클라이언트 UX/계정-캐릭터 목록 API 설계 추가 필요 |
| login 단계의 world 선택 | login이 center에 직접 zone endpoint 조회 | account가 world route를 선택 | 방향은 맞지만 world/char selection API 정교화 필요 |
| 플레이 가능 상태 완료 시점 | zone의 `REGISTER_AVATAR_POST_RECV` | world enter result + zone ack 기반 | 신규 쪽이 더 명시적이고 좋음 |
| PLAYUSER 레지스트리 | 별도 서버 | world/account 내부 상태로 흡수 | 그대로 복붙하면 안 됨 |
| CENTER의 공용 상태 분배 | 별도 중앙 서버 | world/account/control로 분산 | 레거시 구조를 그대로 가져오면 경계가 다시 흐려짐 |

## 레거시 흐름을 new_server에 그대로 복붙하면 안 되는 이유

1. 레거시는 `LOGIN`이 인증, 캐릭터 로딩, zone endpoint 선택, 일부 입장 진행 플래그까지 다 쥐고 있어 경계가 넓다.
2. 레거시는 `ZONE`이 최종 세션 소유권과 월드 상태 초기화, 파티/길드/캐시/시간/이벤트 전송을 모두 맡아 giant server가 된다.
3. 레거시는 `PLAYUSER`와 `CENTER`라는 중간 상태 서버가 강결합돼 있어 source of truth가 분산된다.
4. 레거시는 `IP/PORT`를 직접 넘겨 주는 구조라 replay 방지, stale route 처리, reconnect 정책이 약하다.
5. 레거시의 `MyUser`, `MyGame`, `MyDB`는 global mutable singleton이라 `new_server`의 `runtime / handler / repository / proto` 분리를 망가뜨린다.

## 바로 구현 가능한 후속 작업

### new_server에 바로 구현 가능한 최소 단계 5개

1. `account_server`에 "캐릭터 선택 검증 + selected_char_id not found" 실패 코드를 명시적으로 추가한다.
2. `proto/client/login_proto.h`에 로그인 실패 reason을 확장해 `auth_failed`, `world_not_ready`, `character_not_found`를 구분한다.
3. `world_server`의 enter-world 성공 시점 로그를 `zone assign ok`, `zone player enter ack ok`, `account notify ok`로 분리해 가시성을 높인다.
4. `zone_server`의 `OnPlayerEnterRequest_`에 map not found/capacity 관련 메트릭과 로그를 추가한다.
5. `login_server`에서 `world_enter_success_notify` 수신 후 세션 정리 정책을 테스트 케이스로 고정한다.

### 아직 추가 분석이 필요한 레거시 지점 5개

1. `S06_TS_LOGIN`에서 `REGISTER_USER_FOR_LOGIN_1/2`를 호출하는 정확한 지점.
2. `client`가 `tZoneNumber`를 얻는 패킷/화면 흐름.
3. `W_FAIL_MOVE_ZONE_2_SEND`와 실제 zone move rollback의 전후 맥락.
4. `PLAYUSER`의 `mPlayUserExistState` 상태 전이 전체와 reconnect 상호작용.
5. `CENTER`의 `DOUBLE_CONNECT_USER_SEND`가 기존 zone 세션에 어떤 정리 패킷을 보내는지의 종단 흐름.
