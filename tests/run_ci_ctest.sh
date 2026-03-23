#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(pwd)" == "${repo_root}" ]]; then
  build_dir="${BUILD_DIR:-build}"
else
  build_dir="$(pwd)"
fi

if ! command -v ctest >/dev/null 2>&1; then
  echo "ctest not found"
  exit 1
fi

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found"
    exit 1
  fi
  echo "Configuring build directory: ${build_dir}"
  cmake -S "${repo_root}" -B "${build_dir}" -DBUILD_TESTS=ON
fi

cd "${build_dir}"

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
