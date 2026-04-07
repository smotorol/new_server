# AOI Bench Client

## 목적

`BenchDummyClient`는 WinForms 더미 클라이언트와 별도로, 하나의 프로세스 안에서 여러 세션을 동시에 생성해 AOI 부하 테스트를 수행하는 headless 전용 bench 경로입니다.

- WinForms: 디버그, 눈으로 확인, 소규모 재현
- Bench: 중대규모 로그인/입장/이동/portal/reconnect 부하

## 설계 개요

- 실행 파일: `tools/BenchDummyClient/BenchDummyClient.exe`
- 입력: runner/provisioning과 동일한 `AccountsCsv`
- 실행 구조: `1 process -> N sessions`
- 결과물:
  - `bench_results.jsonl`
  - `bench_summary.json`
  - `bench_failures.json`
  - `bench_process.log`

## AccountsCsv 계약

필수 컬럼:

- `LoginId`
- `Password`

권장 컬럼:

- `WorldIndex`
- `CharacterIndex`
- `PortalRoute`
- `ClientTag`

샘플:

```csv
LoginId,Password,WorldIndex,CharacterIndex,PortalRoute,ClientTag
load00001,pw1,0,0,"1,2,3,4",lt-00001
load00002,pw1,0,0,"1,2,3,4",lt-00002
```

## Bench 단독 실행

```powershell
G:\Programing\Work\new_server\tools\BenchDummyClient\bin\Debug\BenchDummyClient.exe `
  --accounts-csv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  --session-count 100 `
  --duration-sec 300 `
  --move-interval-ms 250 `
  --portal-interval-sec 20 `
  --result-dir G:\Programing\Work\new_server\out\load_tests\bench_manual_001 `
  --process-label bench_manual_001
```

## Runner 연계

runner는 `-ClientMode Bench`로 bench 프로세스를 띄울 수 있습니다.

```powershell
& G:\Programing\Work\new_server\scripts\aoi_load_test_runner.ps1 `
  -ClientMode Bench `
  -AccountsCsv G:\Programing\Work\new_server\config\loadtest_accounts.csv `
  -Clients 300 `
  -BenchProcessCount 2 `
  -DurationSeconds 600 `
  -MaxConcurrentConnect 50
```

동작 방식:

- `Clients` 수를 `BenchProcessCount`로 분할
- 각 bench 프로세스에 `account-start-index`와 `session-count`를 넘김
- process 결과는 `run_dir/bench_processes/bench_xxxx/` 아래에 저장
- runner summary는 process summary를 다시 aggregate

## 권장 사용 방식

- `1~20`: WinForms 또는 headless WinForms
- `20~200`: Bench 1프로세스
- `200~1000`: Bench 다중 프로세스
- `1000+`: 멀티 호스트 분산 + process당 session 수 조절

## 결과 해석

runner Bench 모드 주요 산출물:

- `run_manifest.json`
- `run_summary.json`
- `run_summary.txt`
- `bench_processes/bench_0001/bench_summary.json`
- `bench_processes/bench_0001/bench_results.jsonl`

`run_summary.json`은 process-level aggregate 결과이고, 세부 session 실패는 각 `bench_results.jsonl`을 확인합니다.

## 운영 순서

1. `aoi_provision_loadtest_accounts.ps1`로 계정/캐릭터 준비
2. `aoi_load_test_runner.ps1 -ClientMode Bench` 실행
3. `out/load_tests/run_xxx/`에서 bench/process/server/analyzer 결과 확인
4. 필요 시 `scripts/aoi_analyzer.py` 결과와 함께 비교

