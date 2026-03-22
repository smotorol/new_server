# new_server Development Environment Setup

## 1. 프로젝트 구조

    new_server
    │
    ├─ CMakeLists.txt
    ├─ CMakePresets.json
    ├─ vcpkg.json
    │
    ├─ dev_env.sh
    ├─ dev_services.sh
    ├─ setup_windows_oneclick.bat
    │
    ├─ dockerredis/
    │
    ├─ dependence/
	│	├─ Docker Desktop Installer.exe
    │   ├─ cmake-4.3.0-rc2-windows-x86_64.msi
    │   ├─ msodbcsql.msi
    │   ├─ wsl.2.6.3.0.x64.msi
    │   └─ wsl_update_x64.msi
    │
    └─ README.md

------------------------------------------------------------------------

## 2. Windows 의존성 설치

dependence 폴더에 Windows 개발환경 설치 파일이 포함되어 있다.

    dependence/
	  Docker Desktop Installer.exe
      cmake-4.3.0-rc2-windows-x86_64.msi
      msodbcsql.msi
      wsl.2.6.3.0.x64.msi
      wsl_update_x64.msi

### 2.1 WSL 설치

readme_wsl.txt 참고

------------------------------------------------------------------------

### 2.2 CMake 설치

    cmake-4.3.0-rc2-windows-x86_64.msi

설치 시 옵션:

    Add CMake to system PATH

확인:

    cmake --version

------------------------------------------------------------------------

### 2.3 SQL Server ODBC Driver

    msodbcsql.msi

이 드라이버는 서버가 MSSQL에 연결할 때 필요하다.

------------------------------------------------------------------------

## 3. Windows 개발환경 구축

루트에서 실행:

    setup_windows_oneclick.bat

자동 수행 작업:

-   external/vcpkg clone
-   external/inipp_repo clone
-   vcpkg bootstrap
-   vcpkg dependency install
-   cmake configure
-   cmake build

------------------------------------------------------------------------

## 4. WSL 개발환경 구축

WSL Ubuntu 실행 후:

    ./dev_env.sh

자동 수행 작업:

-   Ubuntu build dependency 설치
-   Microsoft ODBC Driver 18 설치
-   vcpkg bootstrap
-   vcpkg dependency install
-   dockerredis stack 초기화
-   cmake configure
-   cmake build

------------------------------------------------------------------------

## 5. Docker 서비스 관리

dockerredis 스택 관리:

    ./dev_services.sh up
    ./dev_services.sh down
    ./dev_services.sh restart
    ./dev_services.sh ps
    ./dev_services.sh logs

------------------------------------------------------------------------

## 6. dockerredis 초기화

초기 환경 설정:

    dockerredis/init.sh

수행 작업:

-   기존 컨테이너 제거
-   docker compose down
-   docker compose up -d

------------------------------------------------------------------------

## 7. 빌드

Linux Debug:

    cmake --preset linux-debug
    cmake --build --preset linux-debug-build

Release:

    cmake --preset linux-release
    cmake --build --preset linux-release-build

------------------------------------------------------------------------

## 8. Visual Studio 2022 사용

1.  Open Folder
2.  프로젝트 루트 선택
3.  CMake Preset 선택

```{=html}
<!-- -->
```
    windows-debug
    linux-debug

캐시 문제 발생 시:

    Delete Cache and Reconfigure

------------------------------------------------------------------------

## 9. One‑Click 개발환경

WSL:

    ./dev_env.sh

Windows:

    setup_windows_oneclick.bat

------------------------------------------------------------------------

## 10. 문제 해결


------------------------------------------------------------------------

## 11. 권장 개발 흐름

    setup_windows_oneclick.bat
    WSL Ubuntu 실행
    ./dev_env.sh

------------------------------------------------------------------------

## 12. 세션/인증/라인 책임 정책

- 운영형 세션 정책 문서: `docs/server_session_policy.md`
- Login/World/Control 경계, 중복 로그인 처리, `(account_id, char_id)` authoritative 바인딩 원칙을 정리했다.
