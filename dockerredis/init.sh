#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

log()  { printf '\n[%s] %s\n' "INFO" "$*"; }
warn() { printf '\n[%s] %s\n' "WARN" "$*" >&2; }
die()  { printf '\n[%s] %s\n' "ERROR" "$*" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || die "docker command not found"
docker compose version >/dev/null 2>&1 || die "docker compose plugin not found"

cd "$SCRIPT_DIR"

CONTAINERS=(
  dc_mssql
  dc_mssql_init
  dc_redis
  dc_redis_replica1
  dc_mysql
)

log "Removing fixed-name containers if they already exist"
for name in "${CONTAINERS[@]}"; do
  if docker ps -a --format '{{.Names}}' | grep -Fxq "$name"; then
    docker rm -f "$name" >/dev/null 2>&1 || warn "Failed to remove container: $name"
    log "Removed container: $name"
  fi
done

log "Stopping previous compose stack"
docker compose down -v --remove-orphans || true

log "Starting compose stack"
docker compose up -d

log "Development stack is up"
