#!/usr/bin/env bash
set -euo pipefail

SERVER="mssql"
USER="sa"
PASS="${SA_PASSWORD}"
DB="${TARGET_DB}"

# sqlcmd 경로 탐색
if [ -x "/opt/mssql-tools18/bin/sqlcmd" ]; then
  SQLCMD="/opt/mssql-tools18/bin/sqlcmd"
elif [ -x "/opt/mssql-tools/bin/sqlcmd" ]; then
  SQLCMD="/opt/mssql-tools/bin/sqlcmd"
else
  echo "[init] ERROR: sqlcmd not found"
  exit 1
fi

echo "[init] waiting for SQL Server..."
for i in $(seq 1 60); do
  if ${SQLCMD} -S "${SERVER}" -U "${USER}" -P "${PASS}" -Q "SELECT 1" >/dev/null 2>&1; then
    echo "[init] SQL Server is ready."
    break
  fi
  sleep 2
done

echo "[init] create database: ${DB}"
DBNAME="${DB}" ${SQLCMD} -S "${SERVER}" -U "${USER}" -P "${PASS}" \
  -i /init/00_create_db.sql

echo "[init] done."