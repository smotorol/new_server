@echo off
setlocal

REM 항상 이 bat 파일 위치에서 실행
cd /d "%~dp0"

echo ==============================
echo Docker simple reset
echo ==============================

REM Docker 실행 체크
docker version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Docker not running
    exit /b 1
)

REM ------------------------------
REM "있으면 rm, 없으면 무시"
REM ------------------------------
docker rm -f dc_mssql           >nul 2>&1
docker rm -f dc_mssql_init      >nul 2>&1
docker rm -f dc_redis           >nul 2>&1
docker rm -f dc_redis_replica1  >nul 2>&1
docker rm -f dc_mysql           >nul 2>&1

REM ------------------------------
REM compose 전체 정리 + 재기동
REM ------------------------------
docker compose down -v
docker compose up -d

echo ==============================
echo DONE
echo ==============================

exit /b 0