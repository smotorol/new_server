@echo off

:: 관리자 권한 체크
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting admin privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Killing DummyClientWinForms.exe...

taskkill /IM DummyClientWinForms.exe /F /T >nul 2>&1

echo Done.
pause