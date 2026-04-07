# AOI Load Test

## 사전 준비

- 계정은 미리 생성되어 있어야 합니다.
- DummyClient 자동화 빌드 산출물:
  - `tools/DummyClientWinForms/bin/Debug/DummyClientWinForms.exe`
- 서버 설정은 `config/*.ini` 와 `config/servers.json` 기준입니다.
- AOI CSV 로그는 zone 서버에서 아래 플래그가 켜져 있어야 합니다.
  - `EnableAoiPerfFileLog=1`
  - `EnableAoiSummaryFileLog=1`
  - `EnableAoiHotspotFileLog=1`

## AccountsCsv 스키마

필수 컬럼:

- `LoginId`
- `Password`

선택 컬럼:

- `WorldIndex`
- `CharacterIndex`
- `PortalRoute`
- `ClientTag`
- `DurationSeconds`
- `MoveIntervalMs`
- `PortalIntervalSeconds`
- `ReconnectIntervalSeconds`
- `WanderRadius`

정책:

- `LoginId` 중복은 허용하지 않습니다.
- `Clients` 수보다 행 수가 적으면 실행을 중단합니다.
- 선택 컬럼이 비어 있으면 runner 인자 기본값을 사용합니다.

## 검증만 수행

```powershell
& G:\Programing\Work\new_server\scripts\aoi_load_test_runner.ps1 `
  -AccountsCsv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  -Clients 100 `
  -ValidateAccountsOnly
```

## 실행 예시

### 100 clients

```powershell
& G:\Programing\Work\new_server\scripts\aoi_load_test_runner.ps1 `
  -AccountsCsv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  -Clients 100 `
  -DurationSeconds 300 `
  -ClientLaunchSpacingMs 75 `
  -MaxParallelLaunch 25
```

### 300 clients

```powershell
& G:\Programing\Work\new_server\scripts\aoi_load_test_runner.ps1 `
  -AccountsCsv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  -Clients 300 `
  -DurationSeconds 600 `
  -ClientLaunchSpacingMs 100 `
  -MaxParallelLaunch 25
```

### 500 clients

```powershell
& G:\Programing\Work\new_server\scripts\aoi_load_test_runner.ps1 `
  -AccountsCsv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  -Clients 500 `
  -DurationSeconds 900 `
  -ClientLaunchSpacingMs 125 `
  -MaxParallelLaunch 20
```

## 결과 디렉터리 구조

```text
out/load_tests/run_YYYY_MM_DD_HHMMSS/
  run_manifest.json
  run_summary.json
  run_summary.txt
  client_logs/
  client_results/
  server_logs/
  server_logs_pre_run/
  analyzer/
```

## 주요 실패 유형

- `login_failed`: 계정/비밀번호 오류 또는 로그인 서버 응답 실패
- `character_list_empty`: 캐릭터가 없는 계정
- `enter_world_failed`: 월드 입장 실패
- `reconnect_failed`: reconnect 응답 실패
- `missing_result`: 프로세스는 종료했지만 구조화된 결과 파일이 없음
- `launch_failed`: 프로세스 시작 실패

## 결과 확인 순서

1. `run_summary.txt`에서 전체 성공/실패 건수 확인
2. `run_summary.json`에서 실패 사유별 count 확인
3. `client_results/*.json`에서 특정 계정 상태 전이 확인
4. `analyzer/`에서 AOI perf report 확인
5. `server_logs/`에서 zone/world 로그와 대조

