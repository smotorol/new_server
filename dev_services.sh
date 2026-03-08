#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
STACK_DIR="$ROOT_DIR/dockerredis"

usage() {
  cat <<USAGE
Usage: ./dev_services.sh <up|down|restart|ps|logs|pull>

Commands:
  up       Start dockerredis stack
  down     Stop dockerredis stack
  restart  Restart dockerredis stack
  ps       Show service status
  logs     Show compose logs
  pull     Pull latest images
USAGE
}

log()  { printf '\n[%s] %s\n' "INFO" "$*"; }
die()  { printf '\n[%s] %s\n' "ERROR" "$*" >&2; exit 1; }

[[ -d "$STACK_DIR" ]] || die "dockerredis directory not found at: $STACK_DIR"
command -v docker >/dev/null 2>&1 || die "docker command not found"
docker compose version >/dev/null 2>&1 || die "docker compose plugin not found"

cmd="${1:-}"
case "$cmd" in
  up)
    log "Starting stack"
    (cd "$STACK_DIR" && docker compose up -d)
    ;;
  down)
    log "Stopping stack"
    (cd "$STACK_DIR" && docker compose down)
    ;;
  restart)
    log "Restarting stack"
    (cd "$STACK_DIR" && docker compose down && docker compose up -d)
    ;;
  ps)
    (cd "$STACK_DIR" && docker compose ps)
    ;;
  logs)
    (cd "$STACK_DIR" && docker compose logs --tail=200 -f)
    ;;
  pull)
    (cd "$STACK_DIR" && docker compose pull)
    ;;
  -h|--help|"")
    usage
    ;;
  *)
    die "Unknown command: $cmd"
    ;;
esac
