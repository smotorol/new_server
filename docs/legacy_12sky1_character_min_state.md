# Legacy 12Sky1 Character Minimum State

## 개요

이 문서는 `12sky1`의 `AvatarInfo` 스키마와 실제 서버 코드 소비 지점을 기준으로, `new_server`에서 로그인 후 월드 입장과 기본 플레이 시작에 필요한 "캐릭터 최소 상태 모델"을 재정의한 설계 메모다.

핵심 결론은 다음과 같다.

- `확정`: 레거시는 `dbo.AvatarInfo` 한 행에 로그인 화면, 월드 입장, 장비/인벤토리, 소셜, 이벤트, 저장 상태를 모두 밀어 넣는 거대한 구조다.
- `확정`: 로그인 직후 실제로 필요한 것은 전체 `AvatarInfo`가 아니라 `character select summary`다.
- `확정`: 월드 입장 직전과 존 스폰 시점에는 이름/종족/외형/레벨/HP/MP/위치/장비 요약/길드 요약처럼 훨씬 작은 집합만 hot path에 필요하다.
- `확정`: `S06_TS_LOGIN::DB_LOAD_AVATAR`는 매우 큰 컬럼 묶음을 읽지만, `W_DEMAND_USER_AVATAR`와 `S07_TS_ZONE::W_REGISTER_AVATAR_SEND`에서 즉시 소비되는 필드는 제한적이다.
- `확정`: 레거시의 `aLogoutInfo[0..5]`는 위치와 생명력/내공 같은 "재접속 가능한 직전 상태"를 보관하는 데 쓰인다.
- `확정`: `S07_TS_ZONE`은 플레이 시작 시 `mUSER[].mAvatarInfo`에서 `mAVATAR_OBJECT`를 구성하므로, 신규 구조에서도 world enter snapshot이 별도로 필요하다.
- `추정`: 레거시의 일부 저장 필드는 월드 런타임이 아니라 이벤트/운영 기능 누적 때문에 계속 덧붙은 것으로 보이며 1차 플레이 루프에 필수는 아니다.
- `권장`: `new_server`는 `character identity/basic metadata`, `runtime hot state`, `extended lazy state` 3계층으로 분리해야 한다.
- `권장`: `account`는 캐릭터 선택 화면용 요약의 source of truth, `world`는 입장 이후 hot runtime state의 source of truth가 되는 편이 안전하다.
- `권장`: `char_id`를 안정 식별자로 사용하고, 레거시처럼 `aName`을 PK처럼 취급하지 않는다.

## 레거시 캐릭터 데이터 구조 요약

주요 근거:

- 스키마: [dbo.AvatarInfo.Table.sql](G:\Programing\Work\12sky1\12sky1\DB\dbo.AvatarInfo.Table.sql)
- 로그인 로드: [S06_MyDB.cpp](G:\Programing\Work\12sky1\12sky1\S06_TS_LOGIN\S06_MyDB.cpp)
- 로그인 후 캐릭터 목록 처리: [S04_MyWork02.cpp](G:\Programing\Work\12sky1\12sky1\S06_TS_LOGIN\S04_MyWork02.cpp)
- 존 입장 시 아바타 초기화: [S04_MyWork02.cpp](G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\S04_MyWork02.cpp)
- 플레이유저 레지스트리/재접속 상태: [S07_MyGame01.cpp](G:\Programing\Work\12sky1\12sky1\S01_TS_PLAYUSER\S07_MyGame01.cpp)

레거시 `AvatarInfo`는 아래 범주가 한 구조체에 섞여 있다.

| 범주 | 대표 필드 | 관찰 |
| --- | --- | --- |
| 식별/기본 정보 | `uID`, `aName`, `aTribe`, `aGender`, `aHeadType`, `aFaceType` | 로그인 후 목록 표시와 존 스폰 둘 다 사용됨 |
| 진행도/전투 기본치 | `aLevel`, `aGeneralExperience`, `aHonor`, `aVitality`, `aStrength`, `aKi`, `aWisdom` | 스폰/계산의 기본 입력이지만 로그인 화면에서는 일부만 필요 |
| 외형/장비 | `aHeadAddType`, `aFaceAddType`, `aBodyAddType`, `aArmsAddType`, `aEquip`, `aHighLevelEffect` | 캐릭터 렌더링과 초기 전투 파생값에 영향 |
| 위치/이탈 상태 | `aPreviousZoneNumber`, `aLogoutInfo01..06` | 재접속, 맵 복귀, HP/MP 복원에 중요 |
| 소셜 요약 | `aGuildName`, `aGuildGrade`, `aGuildRole`, `aCallName`, `aPartyName`, `aTeacher`, `aStudent` | 초기 플레이에는 일부만 필요, 대부분 lazy 후보 |
| 인벤토리/창고/포션 | `aInventory*`, `aStoreItem*`, `aPotion` | 로그인 목록에는 불필요, 존 입장 후에도 전부 즉시 필요하지 않음 |
| 스킬/핫키 | `aSkill`, `aSkillHotKey` | 전투 개시 전 일부는 필요하지만 전체는 lazy 후보 |
| 랭크/이벤트/확장 기능 | `aRankPoint*`, `aQuestInfo`, `aPat*`, `aCostume*`, 각종 시간 필드 | 1차 구현에서는 다수 제외 가능 |

## 필드별 사용 단계 분석

아래 표는 "스키마에 존재"가 아니라 "코드에서 실제로 소비되는 흔적"을 기준으로 최소 필드 우선순위를 정리한 것이다.

| 필드명 | 의미 | 출처 | 사용 단계 | 필수 여부 | 저장 빈도 |
| --- | --- | --- | --- | --- | --- |
| `account_id` | 계정 식별자 | `AvatarInfo.uID`, `DB_LOAD_AVATAR` | 로그인 인증 후 캐릭터 소유권 검증 | 필수 | 드묾 |
| `char_id` | 신규 안정 캐릭터 식별자 | 레거시 `uID+aName` 조합 대체 제안 | 전체 단계 공통 | 필수 | 드묾 |
| `char_name` | 캐릭터명 | `aName` | 선택 화면, 월드 입장, 스폰 | 필수 | 드묾 |
| `tribe/faction` | 진영/종족 | `aTribe` | 선택 화면, 존 규칙, 스폰 | 필수 | 드묾 |
| `gender` | 성별 | `aGender` | 선택 화면, 스폰 | 필수 | 드묾 |
| `face/head_type` | 기본 얼굴/머리 타입 | `aHeadType`, `aFaceType` | 선택 화면, 스폰 | 필수 | 드묾 |
| `appearance_override` | 추가 외형 파츠 | `aHeadAddType`, `aFaceAddType`, `aBodyAddType`, `aArmsAddType` | 스폰, 렌더링 | 필수 | 드묾 |
| `level` | 레벨 | `aLevel` | 선택 화면, 존 진입 조건, 스폰 | 필수 | 자주 |
| `exp` | 경험치 | `aGeneralExperience` | 월드 런타임, 저장 | 권장 | 자주 |
| `honor` | 명예/평판 성격 수치 | `aHonor` | 스폰 후 상태 패킷, 규칙 분기 | 권장 | 보통 |
| `hp` | 현재 생명력 | `aLogoutInfo[4]` | 월드 입장 직전, 존 스폰 | 필수 | 자주 |
| `mp` | 현재 내공/마나 | `aLogoutInfo[5]` | 월드 입장 직전, 존 스폰 | 필수 | 자주 |
| `zone_id` | 현재 또는 복귀 존 | `aPreviousZoneNumber`, `aLogoutInfo[0]` 계열 | 월드 route 결정, reconnect | 필수 | 자주 |
| `map_id` | 맵/필드 식별 | `aLogoutInfo[1]` 계열 추정 | zone spawn, reconnect | 필수 | 자주 |
| `position` | 좌표 | `aLogoutInfo[2..3]` 계열 추정 | zone spawn, reconnect | 필수 | 자주 |
| `equip_summary` | 장착 장비 요약 | `aEquip`, `aEquipValue` | 스폰 외형/전투 파생치 | 필수 | 보통 |
| `skill_summary` | 전투 시작에 필요한 최소 스킬 요약 | `aSkill`, `aSkillHotKey` | 기본 전투 가능 여부 | 권장 | 보통 |
| `guild_summary` | 길드명/등급/역할/호칭 | `aGuildName`, `aGuildGrade`, `aGuildRole`, `aCallName` | 이름판/버프/소셜 UI | 선택 | 드묾 |
| `party_summary` | 파티명/파티 상태 | `aPartyName` | 존 입장 직후 UI/상태 | 선택 | 보통 |
| `kill_other_tribe` | 적대 진영 처치 수 | `aKillOtherTribe` | 일부 존 진입 조건 | 선택 | 보통 |
| `online_state` | 온라인/오프라인 상태 | `PLAYUSER` 상태, 신규 런타임 제안 | 중복 로그인, reconnect | 필수 | 자주 |
| `last_login/logout` | 마지막 접속 시각 | 레거시 직접 사용 흔적 약함, 신규 운영용 제안 | 운영/감사 | 선택 | 보통 |
| `deleted/blocked` | 삭제/차단 상태 | 계정/캐릭터 필터링 제안 | 로그인 직후, 선택 전 | 필수 | 드묾 |

판정 메모:

- `확정`: `char_name`, `tribe`, `gender`, `head/face`, `appearance_override`, `level`, `hp`, `mp`, `equip_summary`, `guild_summary 일부`, `kill_other_tribe`는 실제 소비 흔적이 있다.
- `추정`: `map_id`, `position`의 정확한 `aLogoutInfo` 인덱스 의미는 별도 추가 분석이 필요하지만, 위치 계열 상태가 필요하다는 점 자체는 확정이다.

## 최소 상태 모델 제안

레거시 `AVATAR_INFO`를 그대로 옮기지 말고 아래 3층으로 분리한다.

### 1. identity/basic metadata

로그인 후 캐릭터 선택과 소유권 검증에 필요한 정보만 둔다.

```cpp
struct CharacterIdentity {
    std::uint64_t account_id;
    std::uint64_t char_id;
    std::string char_name;
    std::uint8_t faction;
    std::uint8_t gender;
    std::uint16_t class_id; // 레거시 tribe/job 분리 필요 시 신규 정의
    std::uint16_t level;
    AppearanceSummary appearance;
    CharacterFlags flags; // deleted, blocked, selectable
};
```

### 2. runtime hot state

월드 입장 직전부터 disconnect flush까지 world가 책임질 상태다.

```cpp
struct CharacterRuntimeHotState {
    std::uint64_t char_id;
    std::uint32_t zone_id;
    std::uint32_t map_id;
    Vec2i position;
    std::int32_t hp;
    std::int32_t mp;
    std::uint64_t exp;
    std::uint32_t level;
    std::uint32_t honor;
    EquipSummary equip;
    SkillRuntimeSummary skills;
    SocialSummary social;
    std::uint32_t online_state;
    std::uint32_t version;
};
```

### 3. extended/lazy-loaded state

존 스폰 직후 당장 없더라도 플레이 시작이 가능한 데이터는 분리한다.

```cpp
struct CharacterExtendedState {
    InventorySummary inventory;
    StorageSummary storage;
    QuickslotSummary quickslots;
    QuestSummary quests;
    CostumeSummary costumes;
    GuildExtendedState guild;
    EventProgressSummary events;
};
```

설계 원칙:

- `account`는 `CharacterIdentity`와 `character select summary`를 제공한다.
- `world`는 `CharacterRuntimeHotState`를 소유하고 dirty tracking을 한다.
- `zone`은 `world`가 내려준 hot snapshot만 사용해 플레이 시작을 완성한다.
- `extended state`는 필요 시 로드하거나, 입장 성공 후 비동기로 채운다.

## hot/cold 데이터 분리

### immediate load / lazy load 분류표

| 분류 | 필드 | 이유 |
| --- | --- | --- |
| immediate load | `account_id`, `char_id`, `char_name` | 선택/인증/세션 바인딩의 핵심 |
| immediate load | `faction`, `gender`, `appearance` | 선택 화면과 스폰 외형에 즉시 필요 |
| immediate load | `level`, `exp`, `hp`, `mp` | 진입 가능 여부와 기본 상태 구성에 필요 |
| immediate load | `zone_id`, `map_id`, `position` | 월드 route와 spawn 위치 계산에 필요 |
| immediate load | `equip_summary` | 외형, 전투 파생치, 기본 공격 세팅에 필요 |
| immediate load | `online_state`, `version` | 중복 로그인, reconnect, flush 제어에 필요 |
| immediate load | `guild_summary 최소치` | 이름판/권한 제한이 있으면 즉시 필요 |
| lazy load | 전체 `inventory` | 입장 후 UI 열기 전까진 전체 로드 불필요 |
| lazy load | 전체 `storage`/창고 | 월드 입장 즉시 사용되지 않음 |
| lazy load | 전체 `skill_hotkey` | 기본 전투 스킬 세트만 먼저 필요 |
| lazy load | `quest/event/costume` 세부 필드 | 1차 플레이 루프 핵심이 아님 |
| lazy load | `friend/teacher/student` 상세 | 기본 플레이 시작과 직접 관련 낮음 |
| lazy load | 장기 누적 운영/랭크 상세 | 비동기 집계나 후속 로딩 대상 |

### 저장 빈도 기준 분리

| 분류 | 대표 필드 | 제안 |
| --- | --- | --- |
| 자주 저장 | `zone_id`, `map_id`, `position`, `hp`, `mp`, `exp`, `online_state` | hot row 또는 hot blob로 관리 |
| 중간 빈도 | `level`, `honor`, `equip_summary`, `skill_summary` | dirty flag 기반 주기 flush |
| 드물게 저장 | `char_name`, `appearance`, `guild_summary`, `blocked/deleted` | 이벤트 발생 시 즉시 또는 별도 갱신 |
| 초기 단계 생략 가능 | 전체 창고, 친구 목록, 교사/제자, 이벤트 누적, 대부분 퀘스트 상세 | 1차 구현 제외 |

## 저장 전략 제안

레거시처럼 모든 값을 매번 한 덩어리로 저장하지 않고, 신규는 저장 목적별로 나눈다.

| 전략 | 대상 | 설명 |
| --- | --- | --- |
| immediate save | `online_state`, `last_login`, `last_logout`, duplicate login resolution metadata | 인증/세션 진실성 유지 |
| periodic save | `zone_id`, `map_id`, `position`, `hp`, `mp`, `exp`, `version` | world runtime 주기 flush |
| disconnect flush | hot state 전체 + dirty `equip_summary`/`skill_summary` | 정상 종료 시점 정합성 확보 |
| write-behind candidate | `inventory`, `storage`, `quest`, `costume`, `social extended` | dirty aggregate 단위 비동기 반영 |

권장 메모:

- `account`는 로그인/선택 검증용 읽기 중심 저장소에 가깝게 둔다.
- `world`는 플레이 도중 변하는 hot state 저장의 owner가 된다.
- `zone`은 저장 주체가 아니라 hot state 소비자여야 한다.

## new_server 배치안

현재 구조상 `world/common`에는 샘플 `DemoCharState`만 있고, `services/world/db`는 아직 없다. 따라서 아래 경로는 "신설 제안"이다.

### new_server 배치 제안표

| 제안 위치 | 역할 | 배치 이유 | 상태 |
| --- | --- | --- | --- |
| `src/services/account/runtime/account_line_runtime.*` | 캐릭터 선택 화면용 identity 로드/선택 검증 | 로그인 후 아직 world 소유가 아니기 때문 | 기존 확장 |
| `src/services/world/common/character_identity.h` | `CharacterIdentity`, `AppearanceSummary`, `CharacterFlags` | 로그인/선택 단계와 공용 타입 분리 | 신설 제안 |
| `src/services/world/common/character_runtime_state.h` | `CharacterRuntimeHotState`, `EquipSummary`, `SocialSummary` | world enter 이후 hot state owner를 명확히 함 | 신설 제안 |
| `src/services/world/common/character_extended_state.h` | lazy-loaded 확장 상태 | hot path와 cold data 분리 | 신설 제안 |
| `src/services/world/runtime/world_runtime_enter_world.cpp` | hot snapshot consume, session bind, duplicate login 처리 | 현재 enter-world 흐름과 직접 연결 | 기존 확장 |
| `src/services/world/runtime/world_runtime_persistence.cpp` | 주기 flush, disconnect flush orchestration | 이미 persistence 책임이 모여 있음 | 기존 확장 |
| `src/services/world/db/world_character_repository.*` | hot state load/save repository | world 전용 persistence API 분리 | 신설 제안 |
| `src/db/core/dqs_payloads.h` | character select / enter / flush payload 정의 | DQS 요청 포맷 표준화 | 기존 확장 |
| `src/db/core/dqs_results.h` | select list / enter snapshot / flush result 정의 | 결과 타입 표준화 | 기존 확장 |
| `src/proto/client/login_proto.h` | character select summary 응답 | 선택 화면 프로토콜 | 기존 확장 |
| `src/proto/client/world_proto.h` | world enter ack 이후 최소 상태 패킷 | 월드 입장 후 기본 플레이 패킷 | 기존 확장 |
| `src/proto/internal/login_account_proto.h` | 선택 캐릭터 검증/요약 | login-account 경계 유지 | 기존 확장 |
| `src/proto/internal/world_zone_proto.h` | zone spawn용 hot snapshot | world-zone 경계 유지 | 기존 확장 |
| `src/server_common/registry/session_char_registry.h` | session-char binding 유지 | reconnect/stale close 대응 | 기존 활용 |

## 후속 구현 단계

1. `CharacterIdentity`와 `CharacterRuntimeHotState`를 먼저 정의한다.
2. `account`에서 캐릭터 선택 화면용 summary query/result를 추가한다.
3. `world`에서 enter-world snapshot query/result를 추가한다.
4. `world_runtime_enter_world.cpp`에서 hot snapshot을 받아 session bind와 duplicate login 정책에 연결한다.
5. `world_runtime_persistence.cpp`에서 `position/hp/mp/exp/version` 중심 dirty flush를 붙인다.
6. `inventory/quest/costume`은 lazy load로 미루고, 기본 플레이 루프부터 검증한다.

## new_server에 바로 추가할 최소 struct 후보

```cpp
struct AppearanceSummary {
    std::uint8_t gender;
    std::uint8_t face_type;
    std::uint8_t head_type;
    std::uint8_t head_add_type;
    std::uint8_t face_add_type;
    std::uint8_t body_add_type;
    std::uint8_t arms_add_type;
};

struct EquipSummary {
    std::array<std::uint32_t, 16> item_ids{};
    std::array<std::uint16_t, 16> item_values{};
};

struct SocialSummary {
    std::string guild_name;
    std::uint8_t guild_grade = 0;
    std::uint8_t guild_role = 0;
    std::string call_name;
    std::string party_name;
};

struct CharacterIdentity {
    std::uint64_t account_id = 0;
    std::uint64_t char_id = 0;
    std::string char_name;
    std::uint8_t faction = 0;
    std::uint16_t level = 1;
    AppearanceSummary appearance{};
    bool blocked = false;
    bool deleted = false;
};

struct CharacterRuntimeHotState {
    std::uint64_t char_id = 0;
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    Vec2i position{};
    std::int32_t hp = 0;
    std::int32_t mp = 0;
    std::uint64_t exp = 0;
    std::uint32_t level = 1;
    std::uint32_t honor = 0;
    std::uint32_t kill_other_tribe = 0;
    EquipSummary equip{};
    SocialSummary social{};
    std::uint32_t online_state = 0;
    std::uint32_t version = 0;
};
```

## DB repository에 필요한 query/result/payload 후보

| 종류 | 후보 이름 | 용도 |
| --- | --- | --- |
| query | `LoadCharacterSelectListByAccount` | 계정 기준 캐릭터 선택 화면 목록 |
| query | `LoadCharacterEnterSnapshot` | world enter 직전 hot snapshot |
| query | `ValidateCharacterOwnership` | `account_id-char_id` 소유권 검증 |
| query | `MarkCharacterOnlineState` | 로그인/로그아웃/중복 로그인 처리 |
| query | `FlushCharacterHotState` | `position/hp/mp/exp/zone/version` 저장 |
| query | `FlushCharacterEquipmentSummary` | 장비 변경 dirty save |
| result | `CharacterSelectSummaryRow` | 선택 화면 1행 |
| result | `CharacterEnterSnapshotRow` | world/zone 진입용 최소 스냅샷 |
| result | `CharacterFlushResult` | 저장 성공/충돌/version 갱신 |
| payload | `CharacterSelectListRequest` | account runtime -> DB/DQS |
| payload | `CharacterEnterSnapshotRequest` | world runtime -> DB/DQS |
| payload | `CharacterHotFlushRequest` | world runtime -> DB/DQS |

## 1차 구현에서 제외해도 되는 필드들

- 전체 창고 데이터와 창고 화폐
- 친구 목록, 교사/제자 상세 관계
- 전체 퀘스트 세부 진행값
- 대부분의 이벤트 누적 값과 시즌성 랭크 상세
- 코스튬 만료 상세와 패트/펫 확장 상태
- 전체 스킬 핫키 원본 배열
- 길드 마크/길드 창고/길드 확장 데이터
- 레거시 전용 보정용 필드와 사용 흔적이 희박한 타이머류

## 레거시를 그대로 복제하면 안 되는 이유

- `AvatarInfo`는 한 테이블에 너무 많은 책임이 섞여 있어 hot path 최적화가 어렵다.
- `S06_TS_LOGIN`은 목록 조회 시에도 대량 필드를 한 번에 읽고 후처리까지 수행한다.
- `S07_TS_ZONE`은 런타임 스폰에 필요한 값만 실제로 쓴다.
- 신규 구조는 `login/account/world/zone` 경계를 지키고 token 기반 진입을 유지해야 하므로, 상태도 경계에 맞게 나눠야 한다.
- 따라서 신규에서는 "레거시 컬럼 집합"이 아니라 "신규 단계별 소비 모델"을 기준으로 스키마와 DTO를 다시 정의해야 한다.
