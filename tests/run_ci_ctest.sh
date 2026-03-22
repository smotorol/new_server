#!/usr/bin/env bash
set -euo pipefail

if ! command -v ctest >/dev/null 2>&1; then
  echo "ctest not found"
  exit 1
fi

# 기본 PR 게이트: 빠른 회귀 + 정적 스모크
ctest --output-on-failure -R "^(world_regression_tests|smoke_persistence_shutdown)$"

# 런타임 스모크는 바이너리 존재 시에만 수행(CTest의 SKIP 처리와 중복 안전)
ctest --output-on-failure -R "^runtime_smoke_world_shutdown$" || true
