# Legacy 12Sky1 Server Role Mapping

## 개요

- 목적: 레거시 `12sky1` 서버 프로젝트별 책임을 식별하고, 이를 `new_server`의 서비스/모듈 구조에 대응시키기 위한 분석 문서다.
- 범위: `S01_TS_PLAYUSER`, `S02_TS_CENTER`, `S03_TS_RELAY`, `S04_TS_EXTRA`, `S05_TS_GAMELOG`, `S06_TS_LOGIN`, `S07_TS_ZONE`, `S08_TS_GMTOOL`.
- 대상 신규 구조: `apps/login_server`, `apps/account_server`, `apps/world_server`, `apps/zone_server`, `apps/control_server`, `apps/community_server`, `src/services/*`, `src/net`, `src/proto`, `src/db`, `src/cache`, `src/server_common`.
- 전제: 레거시의 Win32 message loop + global singleton 구조를 그대로 복제하지 않고, `new_server`의 C++20 + Boost.Asio + `runtime / handler / db / proto` 분리 구조를 유지한다.
- 해석 기준:
  - `확정`: 헤더/엔트리포인트/함수명만으로 책임이 직접 드러나는 경우
  - `추정`: 명명 규칙, 연결 대상, 전형적 MMO 서버 책임 분할로 보강한 경우

## 레거시 서버별 책임 요약

| 레거시 서버 | 진입점 / 부트스트랩 근거 | 핵심 책임 | 신규 대응 제안 | 판정 |
| --- | --- | --- | --- | --- |
| `S01_TS_PLAYUSER` | `S01_MainApplication.cpp`, `H01_MainApplication.h`, `H04_MyWork.h`, `H07_MyGame.h` | 로그인/존/툴에서 들어오는 "현재 접속자 상태", 중복 접속, 현재 유저수, 아바타 위치/존 존재 여부 추적 | `account_server`의 세션/계정 중복 제어 일부 + `world_server`의 세션-캐릭터 레지스트리 + `control_server` 운영 조회 일부 | 확정 |
| `S02_TS_CENTER` | `S01_MainApplication.cpp`, `H04_MyWork.h`, `H06_MyDB.h`, `H07_MyGame.h` | 로그인/존/툴 사이의 월드 공용 상태, 월드 정보, 공지/부족/전장/이벤트 조정 | `world_server` 공용 월드 상태 + `control_server` 운영 제어 + 일부는 별도 world-domain/service | 확정 |
| `S03_TS_RELAY` | `S01_MainApplication.cpp`, `H04_MyWork.h`, `H06_MyGame.h` | 비밀/파티/길드 채팅과 파티 관련 상태 중계 | `community_server`가 가장 자연스럽고, 현재 비어 있으므로 신설 우선순위 높음 | 확정 |
| `S04_TS_EXTRA` | `S01_MainApplication.cpp`, `H04_MyWork.h`, `H06_MyDB.h`, `H07_MyGame.h` | 길드, 캐시 아이템, 공용 창고성 저장, 길드 점수/구성원 조회 | `community_server` 또는 `services/community` + `services/account/db` 일부 + 공용 상점/길드 도메인 신설 | 확정 |
| `S05_TS_GAMELOG` | `S01_MainApplication.cpp`, `H02_MyServer.h`, `H03_MyDB.h` | 게임 로그 수신 및 DB 적재 전용 | `src/db` 기반 비동기 적재 워커 또는 별도 `log_service` | 확정 |
| `S06_TS_LOGIN` | `S01_MainApplication.cpp`, `H04_MyWork.h`, `H06_MyDB.h`, `H07_MyGame.h` | 클라이언트 로그인, 아바타 목록/생성/삭제, 월드 입장 전 인증/선택, 차단 IP, 선물/이벤트/이관 처리 | `login_server` + `account_server`로 분리 수용 | 확정 |
| `S07_TS_ZONE` | `S01_MainApplication.cpp`, `H01_MainApplication.h`, `H04_MyWork.h`, `H06_MyGame.h` | 실제 게임 규칙, 전투, 이동, 채팅, 거래, 퀘스트, 파티, 듀얼, 월드/배틀존 처리 | `world_server`의 도메인/세션/상태 전이 + `zone_server`의 맵 인스턴스/수용량/route 분리 | 확정 |
| `S08_TS_GMTOOL` | `S01_MainApplication.cpp`, `H04_MyWork.h`, `H07_MyDB.h` | GM 계정 인증, 권한 변경, 계정/캐릭터/인벤/장비/로그 조회 | `control_server` + 운영용 repository/handler | 확정 |

## 진입점 및 부트스트랩 요약

### 공통 레거시 패턴

- 모든 레거시 서버는 `S01_MainApplication.cpp`의 `WinMain`을 진입점으로 사용한다.
- `H01_MainApplication.h`에 `ReadServerInfo`, `ApplicationInit`, `ApplicationFree`, `WinMainProcedure`가 선언되어 있다.
- `H01_MainApplication.h`가 `MyServer`, `MyUser`, `MyWork`, `MyTransfer`, `MyDB`, `MyGame`를 한 번에 include하며, 사실상 프로세스 전역 singleton 묶음으로 부트스트랩한다.
- `SERVER_INFO` 구조체 안에 상위 서버 IP/Port와 DB 접속 정보가 같이 들어 있어, 네트워크/DB/서버 설정 경계가 분리되지 않았다.

### 레거시 서버별 엔트리 관찰

| 서버 | 엔트리 파일 | 부트스트랩 특징 | 판정 |
| --- | --- | --- | --- |
| `S01_TS_PLAYUSER` | `S01_TS_PLAYUSER/S01_MainApplication.cpp`, `S01_TS_PLAYUSER/H01_MainApplication.h` | Login/Zone/Tool 상위 서버 주소와 DB 2개를 설정에서 직접 읽음 | 확정 |
| `S02_TS_CENTER` | `S02_TS_CENTER/S01_MainApplication.cpp`, `S02_TS_CENTER/H01_MainApplication.h` | WorldNumber, 다중 DB, 공지/월드 상태 중앙 관리형 | 확정 |
| `S03_TS_RELAY` | `S03_TS_RELAY/S01_MainApplication.cpp`, `S03_TS_RELAY/H01_MainApplication.h` | Zone 상위 서버를 받아 relay 용도로 동작 | 확정 |
| `S04_TS_EXTRA` | `S04_TS_EXTRA/S01_MainApplication.cpp`, `S04_TS_EXTRA/H01_MainApplication.h` | Zone과 DB를 연결하고 extra 기능 전담 | 확정 |
| `S05_TS_GAMELOG` | `S05_TS_GAMELOG/S01_MainApplication.cpp`, `S05_TS_GAMELOG/H01_MainApplication.h` | 소켓 수신 후 로그 DB 적재 전용 | 확정 |
| `S06_TS_LOGIN` | `S06_TS_LOGIN/S01_MainApplication.cpp`, `S06_TS_LOGIN/H01_MainApplication.h` | Calendar, DB 3개, 상위 zone/account 성격 연결을 모두 품음 | 확정 |
| `S07_TS_ZONE` | `S07_TS_ZONE/S01_MainApplication.cpp`, `S07_TS_ZONE/H01_MainApplication.h` | PlayUser/Center/Relay/Extra/GameLog/ChatLog까지 다중 상위 서버 연결 | 확정 |
| `S08_TS_GMTOOL` | `S08_TS_GMTOOL/S01_MainApplication.cpp`, `S08_TS_GMTOOL/H01_MainApplication.h` | 운영툴용 독립 서버 + DB 질의 | 확정 |

## 핵심 클래스 분석

### 클래스 패턴

- 공통 핵심 클래스:
  - `MyServer`: 소켓 accept/recv/timer/log
  - `MyWork`: 프로토콜 번호 -> 함수 포인터 디스패치
  - `MyTransfer`: 패킷 송수신 포맷 계층
  - `MyGame`: 실제 메모리 상태와 게임/도메인 로직
  - `MyDB`: DB 쿼리/재접속/직접 저장
  - `MyUser`: 접속 단위 버퍼/유저 세션
- 이 구조는 서버마다 이름은 같지만 책임 비중이 다르다. 특히 `MyWork`가 packet handler registry 역할, `MyGame`이 giant domain object 역할을 맡는다.

### 서버별 핵심 클래스와 책임 단서

| 서버 | 핵심 클래스 | 근거 파일 | 관찰 내용 | 판정 |
| --- | --- | --- | --- | --- |
| `S01_TS_PLAYUSER` | `MyGame`, `MyWork`, `MyDB` | `H07_MyGame.h`, `H04_MyWork.h`, `H06_MyDB.h` | `RegisterUserForLogin_*`, `RegisterUserForZone_*`, `UnRegisterUser`, `ReturnPresentUserNum`, `ReturnExistZoneNumberForAvatarName`가 존재하고 DB는 로그인 상태/현재 유저수/차단/저장 처리 | 확정 |
| `S02_TS_CENTER` | `MyGame`, `MyWork`, `MyDB` | `H07_MyGame.h`, `H04_MyWork.h`, `H06_MyDB.h` | `WORLD_INFO`, `WORLD_INFO2`, `DB_LOAD_WORLD_INFO_FOR_FIRST`, `DB_PROCESS_FOR_BATTLEZONE`, `DB_SAVE_TRIBE_NOTICE_*` 등 월드 전역 운영 상태 보관 | 확정 |
| `S03_TS_RELAY` | `MyWork`, `MyGame` | `H04_MyWork.h`, `H06_MyGame.h` | `W_DEMAND_SECRET_CHAT`, `W_DEMAND_PARTY_*`, `W_DEMAND_GUILD_*`와 `mMap_Party`로 채팅/파티 중계 서버 성격이 명확 | 확정 |
| `S04_TS_EXTRA` | `MyWork`, `MyDB`, `MyGame`, `MyGuild` | `H04_MyWork.h`, `H06_MyDB.h`, `H07_MyGame.h` | 길드 생성/저장/해산/확장, 캐시 잔액/아이템 정보, 길드 저장소, `MyGuild` 캐시 맵 보유 | 확정 |
| `S05_TS_GAMELOG` | `MyServer`, `MyDB` | `H02_MyServer.h`, `H03_MyDB.h` | `DB_PROCESS_SAVE_GAMELOG`, `DB_PROCESS_FOR_GAMELOG`만 존재하는 적재 전용 구조 | 확정 |
| `S06_TS_LOGIN` | `MyWork`, `MyDB`, `MyGame`, `CONVERT_MANAGER` | `H04_MyWork.h`, `H06_MyDB.h`, `H07_MyGame.h` | `W_DEMAND_LOGIN`, `W_DEMAND_USER_AVATAR`, `W_CREATE_AVATAR`, `W_DELETE_AVATAR`, `W_DEMAND_ZONE_SERVER_INFO_1`; DB는 계정/아바타/인벤/스토어/친구/퀘스트/선물성 데이터까지 직접 다룸 | 확정 |
| `S07_TS_ZONE` | `MyServer`, `MyWork`, `MyGame` | `H02_MyServer.h`, `H04_MyWork.h`, `H06_MyGame.h` | `MyWork`에 전투/아이템/파티/채팅/거래/퀘스트/길드/GM 명령이 밀집, `MyGame`은 배틀존/레이스/던전/드롭/이벤트/월드 상태를 거대하게 보유 | 확정 |
| `S08_TS_GMTOOL` | `MyWork`, `MyDB` | `H04_MyWork.h`, `H07_MyDB.h` | 계정 검사, 권한 조회/설정, 로그 조회, 캐릭터/인벤/장비/스토어/포션 조회, 운영 계정 관리 | 확정 |

### 책임 관점별 분류

| 책임 관점 | 주 담당 레거시 서버 | 근거 | 신규 권장 위치 |
| --- | --- | --- | --- |
| 클라이언트 접속 처리 | `S06_TS_LOGIN`, `S07_TS_ZONE` | `W_DEMAND_LOGIN`, `MyServer::PROCESS_FOR_NETWORK`, Zone의 대규모 `MyWork` 핸들러 | `apps/login_server`, `apps/world_server`, `src/server_common/handler`, `src/net` |
| 서버 간 통신 | `S01_TS_PLAYUSER`, `S02_TS_CENTER`, `S03_TS_RELAY`, `S04_TS_EXTRA`, `S07_TS_ZONE` | `LOGIN->CENTER`, `ZONE->RELAY`, `ZONE->EXTRA`, Zone의 다중 upper server | `src/proto/internal`, `server_common/runtime/line_host`, `line_client_host` |
| 인증/로그인 | `S06_TS_LOGIN`, 일부 `S01_TS_PLAYUSER` | 로그인/아바타/존 이동 사전 처리 | `services/login`, `services/account` |
| 캐릭터/아바타 관리 | `S06_TS_LOGIN`, `S01_TS_PLAYUSER`, `S07_TS_ZONE` | 아바타 생성/삭제/선택, 접속 세션 상태, 월드 진입 후 상태 | `services/account/db`, `services/world/runtime`, `services/world/actors` |
| 월드/존/맵 처리 | `S02_TS_CENTER`, `S07_TS_ZONE` | 월드 전역 정보와 zone 전투/맵 진행 | `services/world/runtime`, `services/zone/runtime` |
| 게임 규칙 처리 | `S07_TS_ZONE` | 전투/아이템/퀘스트/상점/길드/거래/이벤트 | `services/world/handler`, `services/world/actors`, 별도 domain 모듈 | 확정 |
| 릴레이/중계 | `S03_TS_RELAY` | 비밀/파티/길드 채팅, 파티 상태 | `services/community` 신설 | 확정 |
| 로그 적재 | `S05_TS_GAMELOG` | 로그 패킷 저장 전용 DB API | `src/db`, 별도 log pipeline | 확정 |
| 운영툴/GM 기능 | `S08_TS_GMTOOL`, 일부 `S02_TS_CENTER` | GM 권한, 계정/로그 조회, 공지/이벤트 설정 | `apps/control_server`, `services/control` |
| DB 연동 | 사실상 전 서버 | `MyDB`가 직접 SQL/저장 로직 보유 | `src/db`, `services/*/db`, repository/job 계층 |

## new_server 구조 관찰

### 엔트리포인트

| 신규 앱 | 근거 파일 | 역할 해석 |
| --- | --- | --- |
| `apps/login_server/main.cpp` | `services/login/runtime/login_line_runtime.h` | 클라이언트 로그인 게이트 + account 연결 |
| `apps/account_server/main.cpp` | `services/account/runtime/account_line_runtime.h` | 로그인 인증, world route 선택, ticket 발급/소비 중개 |
| `apps/world_server/main.cpp` | `services/world/runtime/world_runtime.h` | 인증된 월드 세션, actor, zone route, persistence의 중심 |
| `apps/zone_server/main.cpp` | `services/zone/runtime/zone_runtime.h` | map instance/zone load/heartbeat/월드 연결 |
| `apps/control_server/main.cpp` | `services/control/runtime/control_line_runtime.h` | 운영용 control line + world 연결 |
| `apps/community_server/main.cpp` | 최소 stub | 아직 역할 미구현, relay/extra 수용 후보 |

### 분리 구조 근거

- `src/services/runtime/server_runtime_base.h`
  - 공통 `InitMainThread`, `MainLoop`, `io_context` 관리
  - 레거시 `MyServer + timer + loop`의 공통 런타임 대체물
- `src/server_common/handler/service_line_handler_base.h`
  - 세션 open/close hook과 line handler 추상화
  - 레거시 `MyWork`의 프로토콜 디스패치와는 분리된 네트워크 수용 계층
- `src/proto/internal/*`
  - 서비스 간 내부 프로토콜이 별도 파일로 분리됨
- `src/services/*/db`
  - `account_auth_db_repository`, `account_auth_job`처럼 DB 의존성이 서비스 내부로 캡슐화되기 시작함
- `src/cache/redis/redis_cache.*`
  - 캐시 계층이 이미 분리되어 있어, 레거시의 메모리 글로벌 상태 일부를 대체할 기반이 존재

## new_server 대응 매핑표

| 레거시 서버 | 신규 1차 대응 | 신규 2차 대응 | 이유 |
| --- | --- | --- | --- |
| `S06_TS_LOGIN` | `apps/login_server`, `src/services/login/*` | `apps/account_server`, `src/services/account/*`, `src/proto/internal/login_*`, `src/proto/internal/account_*` | 레거시 login은 클라이언트 인증과 캐릭터/월드 선택을 한 프로세스에서 수행하지만 신규는 `login`과 `account`로 분리됨 |
| `S01_TS_PLAYUSER` | `src/services/account/*` | `src/services/world/runtime/*`, `src/server_common/registry/*`, `apps/control_server` | 접속자 수, 중복 접속, 현재 사용자 조회는 account/world session registry와 운영 조회로 분해하는 편이 맞음 |
| `S02_TS_CENTER` | `src/services/world/runtime/*` | `src/services/control/*`, `src/proto/internal/control_proto.h` | 월드 전역 상태와 운영 브로드캐스트/이벤트는 world 중심, 운영 명령은 control로 분리 |
| `S03_TS_RELAY` | `apps/community_server`, `src/services/community/*` | `src/proto/internal/*`, `src/server_common/runtime/*` | 채팅/파티/길드 notice relay는 현재 신규 구조에서 community가 가장 자연스러운 수용처 |
| `S04_TS_EXTRA` | `src/services/community/*` | `src/services/account/db/*`, `src/cache/*` | 길드/캐시/공용 저장소는 로그인/월드 본체와 분리된 community 또는 별도 guild/shop 도메인으로 분해가 적합 |
| `S05_TS_GAMELOG` | `src/db/*` | 별도 `apps/log_server` 또는 비동기 log ingest 모듈 | 적재 전용 서버는 도메인 runtime보다 ingestion pipeline이 적합 |
| `S07_TS_ZONE` | `apps/world_server`, `src/services/world/*` | `apps/zone_server`, `src/services/zone/*`, `src/proto/client/world_proto.h`, `src/proto/internal/world_zone_proto.h` | 게임 규칙은 world, map/instance route와 load reporting은 zone으로 분리하는 현재 구조와 잘 맞음 |
| `S08_TS_GMTOOL` | `apps/control_server`, `src/services/control/*` | `src/services/*/db`, `src/proto/internal/control_proto.h` | 운영 명령/조회형 기능은 control이 직접 수용하는 것이 자연스러움 |

### 모듈 단위 권장 배치

| 기능 | 권장 위치 | 근거/메모 |
| --- | --- | --- |
| packet decode / dispatch | `src/server_common/handler`, `src/services/*/handler` | 레거시 `MyWork`를 그대로 옮기지 말고 line 별 handler로 분리 |
| service state transition | `src/services/*/runtime` | 세션 생명주기, heartbeat, route registry는 runtime 책임 |
| domain rule | `src/services/world/actors`, `src/services/world/common`, 필요 시 `src/services/community/domain` 신설 | `MyGame` giant class를 기능별 domain object로 해체 |
| db repository | `src/services/*/db`, `src/db/core`, `src/db/shard` | 레거시 `MyDB` 직접 호출 난립을 repository/job 패턴으로 대체 |
| protocol | `src/proto/client`, `src/proto/internal` | 클라이언트 프로토콜과 내부 라인 프로토콜을 분리 유지 |
| global world state | `src/services/world/runtime`, `src/cache/redis` | center/playuser의 전역 메모리 상태를 캐시+runtime로 치환 |
| ops / gm query | `src/services/control`, 각 서비스 전용 read repository | GM 기능은 게임 runtime 내부 침투보다 read-model 기반이 안전 |
| log ingest | `src/db` + 별도 worker/app | game logic과 분리된 비동기 적재가 적합 |

## 이식 전략 분류표

| 레거시 기능군 | 신규 배치 | 분류 | 이유 |
| --- | --- | --- | --- |
| 로그인 요청 수신 / account 연계 | `services/login`, `services/account` | 그대로 이식 가능 | 현재 신규 런타임이 이미 같은 경계로 나뉘어 있음 |
| world route 등록 / world enter ticket | `services/account`, `services/world` | 그대로 이식 가능 | `AccountLineRuntime`, `WorldRuntime`에 대응 구조가 존재 |
| zone route / map instance / player enter-leave | `services/world`, `services/zone` | 그대로 이식 가능 | `world_zone_proto`, `ZoneRuntime`이 이미 수용 가능 |
| 현재 접속자 수 / 중복 접속 방지 | `services/account`, `services/world/runtime`, `server_common/registry` | 구조만 참고 | 레거시 `PLAYUSER`는 필요하지만 별도 서버로 유지할 필요는 낮음 |
| 월드 공지 / 부족 점령 / 전장 스케줄 | `services/world/runtime`, `services/control` | 구조만 참고 | center의 "중앙 월드 상태" 개념은 유효하지만 구현은 재설계 필요 |
| 채팅 relay / 파티 relay | `services/community` | 구조만 참고 | 책임은 살아 있지만 프로세스 경계는 새로 정의해야 함 |
| 길드 / 캐시샵 / 길드 창고 | `services/community` 또는 별도 `guild/shop` 도메인 | 구조만 참고 | 한 서버에 뭉쳐 있던 extra 책임은 신규에서 세분화 필요 |
| GameLog 패킷 수집 후 DB 적재 | `db` worker / 별도 ingest app | 구조만 참고 | 적재 전용 파이프라인으로 바꾸는 편이 적합 |
| Zone giant gameplay handlers | `services/world/*`, 일부 `services/zone/*` | 폐기 또는 재설계 필요 | 현재 `MyWork`/`MyGame` 결합은 신규 구조와 정면 충돌 |
| 로그인 서버의 대규모 직렬화/역직렬화 + 직접 DB 읽기 | `services/account/db`, `proto/*` | 폐기 또는 재설계 필요 | 계정/캐릭터/인벤토리/이벤트 데이터를 runtime에서 직접 만지는 방식은 유지하면 안 됨 |
| GM tool의 직접 DB 조회 남발 | `services/control` + read repository | 폐기 또는 재설계 필요 | 운영 툴은 최소 권한 read-model로 재구성하는 편이 안전 |

## 위험 패턴 및 재설계 포인트

| 위험 패턴 | 관찰 근거 | 영향 | 재설계 포인트 |
| --- | --- | --- | --- |
| 전역 mutable singleton | `extern MyServer mSERVER`, `extern MyGame mGAME`, `extern MyDB mDB`, `extern MyWork mWORK` 전반 | 테스트 어려움, 순서 의존, hidden coupling | runtime 생성자 주입 + 서비스별 상태 캡슐화 |
| 네트워크/DB/게임로직 강결합 | `H01_MainApplication.h`가 모든 계층을 한 번에 include | 변경 비용 증가, 장애 전파 범위 확대 | runtime/handler/domain/repository 분리 유지 |
| giant class | `S07_TS_ZONE/H06_MyGame.h`, `S06_TS_LOGIN/H06_MyDB.h` | 책임 과다, 함수 찾기/검증 어려움 | world session, combat, inventory, guild, event, persistence로 분해 |
| 패킷 처리와 상태 전이 혼합 | `MyWork` 함수 포인터 배열 + `MyGame` 직접 상태 변경 | 핸들러 로직과 도메인 규칙 경계 불명확 | handler는 decode/validation, domain은 state change 담당 |
| DB 직접 호출 난립 | 각 서버 `MyDB`가 저장/조회/로그/운영 쿼리를 모두 직접 보유 | 트랜잭션 경계 불명확, 샤딩/비동기화 어려움 | `services/*/db` repository + job + DQS 패턴 재사용 |
| Win32 message loop 기반 네트워크 | `WinMain`, `WinMainProcedure`, `WM_NETWORK_MESSAGE_*` | 플랫폼 종속, IO concurrency 제한 | `ServerRuntimeBase` + Boost.Asio 유지 |
| 설정 구조 혼합 | `SERVER_INFO` 안에 서버/상위 서버/DB/운영 플래그 혼재 | 배포/환경 분리 어려움 | service config와 infra config 분리 |
| 운영 기능의 본서버 침투 | `ZONE`에 `W_GM_COMMAND_SEND`, `GMTOOL`의 광범위 조회 | 운영 명령이 게임 로직 안전성을 훼손 | control plane와 game plane 분리 |

## 서버별 세부 매핑 메모

### `S06_TS_LOGIN` -> `login_server` + `account_server`

- 확정 근거:
  - `S06_TS_LOGIN/H04_MyWork.h`: `W_DEMAND_LOGIN`, `W_DEMAND_USER_AVATAR`, `W_CREATE_AVATAR`, `W_DELETE_AVATAR`, `W_DEMAND_ZONE_SERVER_INFO_1`
  - `S06_TS_LOGIN/H07_MyGame.h`: block IP, gift item, player count
  - `S06_TS_LOGIN/H06_MyDB.h`: 캐릭터/인벤토리/친구/퀘스트/스토어/이벤트 계열 필드 다수
- 제안:
  - 로그인 handshake, 접속 게이트, 세션 pending: `services/login/runtime`, `services/login/handler`
  - 계정 인증, 캐릭터 선택, world ticket 발급: `services/account/runtime`, `services/account/db`
  - 아바타 직렬화/변환(`CONVERT_MANAGER`) 성격: `services/account/domain` 또는 migration utility

### `S07_TS_ZONE` -> `world_server` + `zone_server`

- 확정 근거:
  - `S07_TS_ZONE/H04_MyWork.h`: 공격, 아이템, 파티, 길드, 거래, 퀘스트, GM, 캐시 아이템, 마켓, 경매 유사 기능
  - `S07_TS_ZONE/H06_MyGame.h`: battle zone, map event, race, hero, dungeon, reward, world info
  - `S07_TS_ZONE/H02_MyServer.h`: Xigncode, socket-user hash, 대규모 접속 처리
- 제안:
  - 전투/세션/캐릭터/월드 룰: `services/world/handler`, `services/world/actors`, `services/world/runtime`
  - zone instance lifecycle, world heartbeat, map assignment: `services/zone/runtime`, `proto/internal/world_zone_proto.h`
  - 클라이언트 메시지 포맷: `proto/client/world_proto.h`

### `S02_TS_CENTER` -> `world_server` + `control_server`

- 확정 근거:
  - `S02_TS_CENTER/H04_MyWork.h`: `LOGIN->CENTER`, `ZONE->CENTER`, `TOOL->CENTER`
  - `S02_TS_CENTER/H06_MyDB.h`: `DB_LOAD_WORLD_INFO_FOR_FIRST`, `DB_PROCESS_FOR_BATTLEZONE`, `DB_SAVE_TRIBE_NOTICE_*`
  - `S02_TS_CENTER/H07_MyGame.h`: `WORLD_INFO`, `WORLD_INFO2`, hero/tribe group/time info
- 제안:
  - 월드 공용 상태는 `WorldRuntime`에서 관리
  - 운영툴성 broadcast/event mutation은 `services/control`로 이동
  - "center"라는 별도 서버 명칭은 유지할 필요 낮음

### `S03_TS_RELAY` + `S04_TS_EXTRA` -> `community_server` 중심

- 확정 근거:
  - Relay는 채팅/파티/길드 중계 전용
  - Extra는 길드/캐시/공용 저장소/길드 멤버 조회 중심
- 제안:
  - 현재 `apps/community_server/main.cpp`가 빈 상태이므로, 레거시 relay/extra를 수용하는 1차 후보로 적절
  - 단, 실제 구현은 최소 두 개의 하위 도메인으로 쪼개는 편이 좋음:
    - community/chat/party/guild notice
    - guild/shop/cash/shared-storage

### `S01_TS_PLAYUSER` -> 별도 서버 복제보다 registry 흡수

- 확정 근거:
  - `RegisterUserForLogin_*`, `RegisterUserForZone_*`, `ReturnPresentUserNum`, `ReturnExistZoneNumberForUserID`
  - Tool에서도 현재 유저수와 아바타 조회 사용
- 제안:
  - 별도 process로 되살리기보다 `account/world`의 session registry + `control` 조회 API로 흡수
  - 캐릭터가 어느 world/zone에 붙어 있는지의 source of truth는 `world_server`가 가져가는 편이 맞음

### `S05_TS_GAMELOG` + `S08_TS_GMTOOL`

- `S05_TS_GAMELOG`
  - 로그 적재 전용이라 game runtime과 분리된 별도 ingestion pipeline이 적합
- `S08_TS_GMTOOL`
  - `control_server`에 수용하되, 각 서비스가 read repository를 제공하는 구조가 바람직
  - 실시간 강제 변경은 제한하고, 조회/감사/권한 변경 중심으로 시작하는 편이 안전

## 추정과 확정 구분 메모

### 확정

- 레거시 전체가 `WinMain` 기반 Win32 서버라는 점
- `S06_TS_LOGIN`이 인증/아바타 선택/존 이동 전 처리를 맡는 점
- `S07_TS_ZONE`이 실제 게임 규칙과 월드/존 진행의 중심이라는 점
- `S03_TS_RELAY`가 채팅/파티/길드 중계라는 점
- `S04_TS_EXTRA`가 길드/캐시/공용 저장소성 기능을 맡는 점
- `S05_TS_GAMELOG`가 로그 적재 전용이라는 점
- `S08_TS_GMTOOL`이 운영툴 DB 조회/권한 관리라는 점

### 추정

- `S04_TS_EXTRA`를 `community_server`가 1차 수용하는 것이 가장 자연스럽다는 점
- `S01_TS_PLAYUSER`를 별도 앱으로 두지 않고 account/world registry로 흡수하는 편이 낫다는 점
- `S02_TS_CENTER`의 일부 운영 제어 책임을 `control_server`에 나누는 것이 바람직하다는 점
- 향후 `community_server` 내부를 chat/guild/shop으로 더 세분화해야 한다는 점

## 다음 단계 추천

1. `services/community` 스켈레톤 설계
   - `S03_TS_RELAY`와 `S04_TS_EXTRA`를 기준으로 `chat relay`, `party/guild relay`, `guild/cash repository` 경계를 먼저 정한다.
2. `world_server` 도메인 분해 설계서 작성
   - `S07_TS_ZONE`의 거대 `MyWork`/`MyGame`를 `combat`, `inventory`, `quest`, `social`, `event`, `gm command`로 나누는 상세 설계가 필요하다.
3. 운영/관측 경계 확정
   - `S01_TS_PLAYUSER`, `S02_TS_CENTER`, `S08_TS_GMTOOL`의 겹치는 운영성 기능을 `control_server`와 read-model repository 기준으로 재분류해야 한다.
