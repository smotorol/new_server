# AOI Load Pipeline

## 목적

`aoi_load_test_runner.ps1`를 기준으로 AOI 부하 테스트의 준비, 실행, 수집, 분석을 한 번의 운영 절차로 묶습니다.

이 문서의 기본 가정은 다음과 같습니다.

- 소규모 시각 확인은 WinForms DummyClient
- 중대규모 실부하는 BenchDummyClient
- 계정/캐릭터 준비는 provisioning 스크립트
- 최종 판정은 runner summary + analyzer 결과

## 역할 분리

- WinForms 모드
  - geometry/minimap/portal 동작을 눈으로 확인
  - 수동 디버그 또는 1~20 클라이언트 수준 확인
- Bench 모드
  - `1 process -> N sessions`
  - 20~1000+ 세션 부하 경로
  - runner의 기본 실부하 경로

## runner 파이프라인 단계

runner manifest의 `pipeline_stages`와 동일합니다.

1. `validate_inputs`
2. `provision_optional`
3. `prepare_run_dir`
4. `start_servers_optional`
5. `launch_clients`
6. `monitor`
7. `collect_outputs`
8. `analyze_optional`
9. `aggregate_summary`
10. `finalize_manifest`

## 주요 입력

- `-ClientMode WinForms|Bench`
- `-AccountsCsv`
- `-Clients`
- `-DurationSeconds`
- `-BenchProcessCount`
- `-MaxConcurrentConnect`
- `-NoServerStart`
- `-NoAnalyzer`

선택적 provisioning hook:

- `-ProvisionBeforeRun`
- `-ProvisionCount`
- `-ProvisionPrefix`
- `-ProvisionCharacterPrefix`
- `-ProvisionValidateOnly`

현재 구조에서는 provisioning hook가 runner 안에서 기존 provisioning 스크립트를 호출해 `AccountsCsv`를 준비합니다.

## artifact 구조

```text
out/load_tests/run_<timestamp>/
  run_manifest.json
  run_summary.json
  run_summary.txt
  inputs/
    loadtest_accounts.csv
    provision_summary.json
  validation/
    validation_report.json
  allocations/
    bench_allocations.json
  bench_processes/
    bench_0001/
      bench_summary.json
      bench_results.jsonl
      bench_failures.json
      bench_process.log
    bench_0002/
      ...
  client_logs/
  client_results/
  server_logs/
  server_logs_pre_run/
  analyzer/
```

## Bench allocation 의미

`allocations/bench_allocations.json`에는 아래가 남습니다.

- `process_index`
- `process_label`
- `account_start_index`
- `session_count`
- `first_login_id`
- `last_login_id`
- `login_ids[]`

즉 어떤 bench process가 어떤 계정 범위를 맡았는지 run 단위로 추적할 수 있습니다.

## summary 해석

공통적으로 확인할 것:

- `requested_clients`
- `launched_clients`
- `launch_failures`
- `success_count`
- `failure_count`
- `login_success_count`
- `enter_world_success_count`
- `reconnect_success_count`
- `disconnected_count`
- `timeout_count`
- `missing_result_count`
- `process_crash_count`
- `analyzer_exit_code`
- `verdict`

Bench 모드 추가 확인:

- `launched_processes`
- `processes[]`
- `bench_processes/*/bench_summary.json`

## exit code 정책

- `0`: 성공, session failure 없음
- `1`: 입력/환경 검증 실패
- `2`: process launch 실패 또는 output 누락/프로세스 crash
- `3`: session failure 존재
- `4`: analyzer 실패

## 권장 운영 순서

1. provisioning
   - 필요하면 runner의 `-ProvisionBeforeRun` 또는 별도 provisioning 실행
2. validation
   - `-ValidateAccountsOnly`
3. runner Bench 실행
4. `run_summary.json` 확인
5. `bench_processes/*/bench_summary.json` 확인
6. `analyzer/` 결과 확인
7. 병목/실패 원인 분석

## 실패 분석 우선순위

1. `validation/validation_report.json`
2. `run_summary.json`
3. `run_manifest.json`
4. `allocations/bench_allocations.json`
5. `bench_processes/*/bench_summary.json`
6. `bench_processes/*/bench_failures.json`
7. `server_logs/`
8. `analyzer/`

## 권장 시나리오

- 100 세션: 1 process Bench
- 300 세션: 1 process Bench 또는 2 process Bench
- 500 세션: 2 process Bench 이상
- 1000+ 세션: multi-host fan-out 고려
