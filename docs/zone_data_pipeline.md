# Zone Data Pipeline

## 목표

`G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA` 의 원본 `WM/WREGION` 포맷을 new_server 런타임에서 직접 읽지 않고, 작업 폴더 기반 중간 포맷과 new_server 전용 binary asset 을 통해 로딩하도록 정리한다.

## source of truth 단계

1. 참고 원본
   - legacy `TS_ZONE\DATA`
   - 읽기 전용 참고/추출 source
2. 작업 폴더 중간 포맷
   - `G:\Programing\Work\new_server\data_src\zone_csv`
   - 사람이 diff/review 가능한 csv
3. 런타임 binary asset
   - `G:\Programing\Work\new_server\resources\zone_runtime.bin`
4. 프로그램 로더
   - `ZoneRuntimeDataStore::LoadFromBinary(...)`
   - DummyClient `GameDataService`

## 추출 대상

- `Z###.WM` -> `maps.csv`
- `Z###_ZONEMOVEREGION.WREGION` -> `portal_regions.csv`
- `Z###_SUMMONNPC.WREGION`, `Z###_SUMMONGUARD*.WREGION` -> `summon_npc_regions.csv`
- `Z###_SUMMONMONSTER*.WREGION` -> `summon_monster_regions.csv`
- `Z###_ZONESAFEREGION.WREGION` -> `safe_regions.csv`
- `Z###_SPECIALREGION.WREGION` -> `special_regions.csv`

## 중간 포맷 스키마

### maps.csv
- `zone_id`
- `map_id`

### portal_regions.csv
- `zone_id`
- `map_id`
- `region_id`
- `value01`
- `value02`
- `value03`
- `value04`
- `center_x`
- `center_y`
- `center_z`
- `radius`
- `dest_zone_id`
- `dest_map_id`
- `dest_x`
- `dest_y`
- `dest_z`

### summon_npc_regions.csv / summon_monster_regions.csv / safe_regions.csv / special_regions.csv
- `zone_id`
- `map_id`
- `region_id`
- `value01`
- `value02`
- `value03`
- `value04`
- `center_x`
- `center_y`
- `center_z`
- `radius`

## 레거시 근거

`데이터설명.txt` 기준:
- `Z***.WM = 존의 월드 지형/충돌/높이 판정용 기본 맵 데이터`
- `Z***_ZONEMOVEREGION.WREGION = 존 이동 트리거(포탈/출구) 영역 정의 데이터`
- `Z***_SUMMONMONSTER*.WREGION = 몬스터 소환 영역`
- `Z***_SUMMONNPC.WREGION = NPC 배치`
- `Z***_ZONESAFEREGION.WREGION = 안전 지역`
- `Z***_SPECIALREGION.WREGION = 특수 이벤트/특수 몬스터 처리용 영역`

legacy source 근거:
- `ServerDefines.h`: `WORLD_REGION_INFO` = `mVALUE01~04`, `mCENTER[3]`, `mRADIUS`
- `S07_MyWorld.cpp`: `*_ZONEMOVEREGION.WREGION` 로드
- `S09_MySummonSystem.cpp`: NPC/monster region center 사용

## builder

실행 파일:
- `G:\Programing\Work\new_server\Bin\Debug\zone_data_builder_d.exe`

기본 동작:
- legacy -> csv 추출
- csv -> `zone_runtime.bin` 빌드

옵션:
- `--legacy-data <path>`
- `--csv-out <path>`
- `--csv-in <path>`
- `--bin-out <path>`
- `--extract-only`
- `--build-only`

예시:

```powershell
& 'G:\Programing\Work\new_server\Bin\Debug\zone_data_builder_d.exe'
& 'G:\Programing\Work\new_server\Bin\Debug\zone_data_builder_d.exe' --extract-only
& 'G:\Programing\Work\new_server\Bin\Debug\zone_data_builder_d.exe' --build-only
```

## 현재 생성 결과

기본 실행 기준:
- maps: 153
- portals: 820
- npcs: 474
- monsters: 22682
- safe: 9
- special: 1625

## binary 형식

- magic: `0x31444E5A`
- version: `1`
- header 에 record count 포함
- 이후 map / portal / npc / monster / safe / special record 순서 저장

## 런타임 연결

서버:
- `WorldRuntime::OnRuntimeInit()` 에서 binary 로드
- `ZoneRuntime::OnRuntimeInit()` 에서 binary 로드 + zone 기준 map prewarm

클라이언트:
- DummyClient `GameDataService` 가 동일 binary 를 읽어 overlay 생성

## portal destination 해석

현재 builder 의 portal 목적지 규칙은 최소 debug/test 목적의 inference 이다.

- `value02 > 0` 이면 `dest_zone_id = value02`
- `dest_map_id = dest_zone_id` 기본 매핑
- destination zone 내부에서 `value02 == source zone` 인 reciprocal portal 을 찾으면 그 center 를 destination 좌표로 사용
- 없으면 source portal center 를 fallback destination 으로 사용

이 부분은 레거시 full portal semantics 를 완전히 복원한 것은 아니며, 후속 티켓에서 더 정확한 destination rule 이 필요할 수 있다.

## 금지 사항

- 프로그램 런타임이 legacy `TS_ZONE\DATA` 원본을 직접 읽지 않는다
- runtime fallback 으로 legacy path direct read 를 두지 않는다
- legacy 원본은 builder/import 단계에서만 참조한다
