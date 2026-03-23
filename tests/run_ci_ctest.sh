#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

if ! command -v ctest >/dev/null 2>&1; then
  echo "ctest not found"
  exit 1
fi

# 이 스크립트는 "빌드 디렉터리"에서 실행해야 한다.
if [[ ! -f CMakeCache.txt ]]; then
  echo "CMakeCache.txt not found. Run this script from a configured build directory."
  exit 1
fi

is_multi_config=0
if grep -q "^CMAKE_CONFIGURATION_TYPES:.*=" CMakeCache.txt; then
  is_multi_config=1
fi

if [[ ${is_multi_config} -eq 1 ]]; then
  build_cfg="${BUILD_CONFIG:-Debug}"
  cmake --build . --config "${build_cfg}" --target world_regression_tests
  # 기본 PR 게이트: 빠른 회귀 + 정적 스모크
  ctest -C "${build_cfg}" --output-on-failure -R "^(world_regression_tests|smoke_persistence_shutdown)$"
else
  cmake --build . --target world_regression_tests
  # 기본 PR 게이트: 빠른 회귀 + 정적 스모크
  ctest --output-on-failure -R "^(world_regression_tests|smoke_persistence_shutdown)$"
fi

# 런타임 로그 시나리오 체커 self-check (샘플 로그)
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --log "${repo_root}/tests/data/runtime_log_sample_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile reconnect_within_grace \
  --log "${repo_root}/tests/data/runtime_log_sample_reconnect_within_grace_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile reconnect_after_grace \
  --log "${repo_root}/tests/data/runtime_log_sample_reconnect_after_grace_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile dup_categories \
  --log "${repo_root}/tests/data/runtime_log_sample_dup_categories_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile auth_threshold_exceeded \
  --log "${repo_root}/tests/data/runtime_log_sample_dup_categories_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile unauth_reject_counted \
  --log "${repo_root}/tests/data/runtime_log_sample_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile flush_success_conflict \
  --log "${repo_root}/tests/data/runtime_log_sample_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile flush_dirty_throughput \
  --log "${repo_root}/tests/data/runtime_log_sample_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile shutdown_clean_drain \
  --log "${repo_root}/tests/data/runtime_log_sample_ok.log"
python3 "${repo_root}/tests/runtime_log_scenario_checks.py" \
  --profile shutdown_timeout \
  --log "${repo_root}/tests/data/runtime_log_sample_shutdown_timeout_ok.log"
