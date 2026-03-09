#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_NAME=$(basename "$0")
ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

BUILD_TYPE="Debug"
RUN_CONFIGURE=1
RUN_BUILD=1
RUN_STACK=0
INSTALL_DOCKER=0
FORCE_CLEAN=0
RUN_SERVER=0
RUN_TEST_CLIENT=0
RUN_TESTS=0
SKIP_APT=0
JOBS=""
PRESET=""
SERVER_BIN=""
TEST_CLIENT_BIN=""

usage() {
  cat <<USAGE
Usage: ./$SCRIPT_NAME [options]

Options:
  --build-type <Debug|Release>  Build type (default: Debug)
  --preset <name>               Override CMake preset (e.g. linux-debug, linux-release)
  --no-configure                Skip cmake configure
  --no-build                    Skip cmake build
  --no-stack                    Do not start docker stack
  --install-docker              Install docker engine / compose plugin on Ubuntu if missing
  --force-clean                 Remove build_linux before configure
  --run-server                  Run built server executable after build
  --run-test-client             Run built test_client executable after build
  --run-tests                   Run ctest if present
  --skip-apt                    Skip apt package installation
  -j, --jobs <N>                Parallel build jobs
  -h, --help                    Show help

Default behavior:
  - Installs Ubuntu build packages
  - Installs Microsoft ODBC Driver 18
  - Prepares vcpkg and external/inipp_repo
  - Bootstraps vcpkg and installs manifest dependencies
  - Runs dockerredis/init.sh if available
  - Configures and builds Linux preset
USAGE
}

log()  { printf '\n[%s] %s\n' "INFO" "$*"; }
warn() { printf '\n[%s] %s\n' "WARN" "$*" >&2; }
die()  { printf '\n[%s] %s\n' "ERROR" "$*" >&2; exit 1; }

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-type)
      [[ $# -ge 2 ]] || die "--build-type requires a value"
      BUILD_TYPE="$2"; shift 2 ;;
    --preset)
      [[ $# -ge 2 ]] || die "--preset requires a value"
      PRESET="$2"; shift 2 ;;
    --no-configure) RUN_CONFIGURE=0; shift ;;
    --no-build) RUN_BUILD=0; shift ;;
    --no-stack) RUN_STACK=0; shift ;;
    --install-docker) INSTALL_DOCKER=1; shift ;;
    --force-clean) FORCE_CLEAN=1; shift ;;
    --run-server) RUN_SERVER=1; shift ;;
    --run-test-client) RUN_TEST_CLIENT=1; shift ;;
    --run-tests) RUN_TESTS=1; shift ;;
    --skip-apt) SKIP_APT=1; shift ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || die "$1 requires a value"
      JOBS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown option: $1" ;;
  esac
done

case "${BUILD_TYPE,,}" in
  debug)
    BUILD_TYPE="Debug"
    DEFAULT_PRESET="linux-debug"
    BUILD_PRESET="linux-debug-build"
    BUILD_DIR="$ROOT_DIR/build_linux/debug"
    ;;
  release)
    BUILD_TYPE="Release"
    DEFAULT_PRESET="linux-release"
    BUILD_PRESET="linux-release-build"
    BUILD_DIR="$ROOT_DIR/build_linux/release"
    ;;
  *)
    die "Unsupported build type: $BUILD_TYPE"
    ;;
esac

[[ -n "$PRESET" ]] || PRESET="$DEFAULT_PRESET"

[[ -f "$ROOT_DIR/CMakeLists.txt" ]] || die "CMakeLists.txt not found in project root"
[[ -f "$ROOT_DIR/CMakePresets.json" ]] || die "CMakePresets.json not found in project root"
[[ -f "$ROOT_DIR/vcpkg.json" ]] || die "vcpkg.json not found in project root"

install_base_packages() {
  if [[ $SKIP_APT -eq 1 ]]; then
    warn "Skipping apt package installation (--skip-apt)"
    return 0
  fi

  log "Installing Ubuntu build dependencies"
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build pkg-config git curl zip unzip tar \
    ca-certificates gnupg lsb-release software-properties-common \
    unixodbc unixodbc-dev
}

install_ms_odbc() {
  if dpkg -s msodbcsql18 >/dev/null 2>&1; then
    log "msodbcsql18 is already installed"
    return 0
  fi

  log "Installing Microsoft ODBC Driver 18 for SQL Server"
  local ubuntu_ver codename keyring repo_file
  ubuntu_ver=$(lsb_release -rs)
  codename=$(lsb_release -cs)
  keyring=/usr/share/keyrings/microsoft-prod.gpg
  repo_file=/etc/apt/sources.list.d/microsoft-prod.list

  if [[ ! -f "$keyring" ]]; then
    curl -fsSL https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor | sudo tee "$keyring" >/dev/null
  fi

  if [[ ! -f "$repo_file" ]]; then
    echo "deb [arch=$(dpkg --print-architecture) signed-by=$keyring] https://packages.microsoft.com/ubuntu/$ubuntu_ver/prod $codename main" | sudo tee "$repo_file" >/dev/null
  fi

  sudo apt-get update
  sudo ACCEPT_EULA=Y DEBIAN_FRONTEND=noninteractive apt-get install -y msodbcsql18 mssql-tools18

  if ! grep -q 'mssql-tools18/bin' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$PATH:/opt/mssql-tools18/bin"' >> "$HOME/.bashrc"
  fi
}

install_docker_if_needed() {
  [[ $INSTALL_DOCKER -eq 1 ]] || return 0

  if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    log "Docker and compose plugin already installed"
    return 0
  fi

  log "Installing docker engine and compose plugin"
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y docker.io docker-compose-v2
  sudo usermod -aG docker "$USER" || true
  sudo systemctl enable --now docker || true
  warn "If docker permission is denied, run: newgrp docker"
}

ensure_submodule_or_clone() {
  local path="$1"
  local repo="$2"
  local branch="${3:-}"

  if [[ -d "$path/.git" || -f "$path/.git" ]]; then
    log "Found repo: ${path#$ROOT_DIR/}"
    return 0
  fi

  if [[ -f "$ROOT_DIR/.gitmodules" ]]; then
    if git config -f "$ROOT_DIR/.gitmodules" --get-regexp '^submodule\..*\.path$' >/dev/null 2>&1; then
      if git config -f "$ROOT_DIR/.gitmodules" --get-regexp '^submodule\..*\.path$' | awk '{print $2}' | grep -Fxq "${path#$ROOT_DIR/}"; then
        log "Initializing submodule: ${path#$ROOT_DIR/}"
        git -C "$ROOT_DIR" submodule update --init --recursive -- "${path#$ROOT_DIR/}"
        return 0
      fi
    fi
  fi

  if [[ -e "$path" && ! -d "$path/.git" && ! -f "$path/.git" ]]; then
    warn "${path#$ROOT_DIR/} exists but is not a git repo; leaving as-is"
    return 0
  fi

  mkdir -p "$(dirname "$path")"
  log "Cloning ${repo} -> ${path#$ROOT_DIR/}"
  if [[ -n "$branch" ]]; then
    git clone --depth 1 --branch "$branch" "$repo" "$path"
  else
    git clone --depth 1 "$repo" "$path"
  fi
}

ensure_vcpkg_root() {
  local default_root="$HOME/vcpkg"

  if [[ -z "${MY_VCPKG_ROOT:-}" ]]; then
    export MY_VCPKG_ROOT="$default_root"
  fi

  if [[ ! -f "$MY_VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
      log "Installing vcpkg into $MY_VCPKG_ROOT"
      rm -rf "$MY_VCPKG_ROOT"
      git clone https://github.com/microsoft/vcpkg "$MY_VCPKG_ROOT"
  else
      log "Using existing vcpkg at $MY_VCPKG_ROOT"
  fi

  if ! grep -q 'export MY_VCPKG_ROOT=' "$HOME/.bashrc" 2>/dev/null; then
    echo "export MY_VCPKG_ROOT=\"$MY_VCPKG_ROOT\"" >> "$HOME/.bashrc"
  fi
  if ! grep -q 'PATH=.*\$MY_VCPKG_ROOT' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$PATH:$MY_VCPKG_ROOT"' >> "$HOME/.bashrc"
  fi

  if ! grep -q 'export MY_VCPKG_ROOT=' "$HOME/.profile" 2>/dev/null; then
    echo "export MY_VCPKG_ROOT=\"$MY_VCPKG_ROOT\"" >> "$HOME/.profile"
  fi
  if ! grep -q 'PATH=.*\$MY_VCPKG_ROOT' "$HOME/.profile" 2>/dev/null; then
    echo 'export PATH="$PATH:$MY_VCPKG_ROOT"' >> "$HOME/.profile"
  fi
}

prepare_external_repos() {
  ensure_submodule_or_clone "$ROOT_DIR/external/inipp_repo" "https://github.com/mcmtroffaes/inipp"
}

bootstrap_vcpkg() {
  log "Bootstrapping vcpkg"
  if [[ ! -f "$MY_VCPKG_ROOT/vcpkg" ]]; then
    bash "$MY_VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

install_vcpkg_packages() {
  log "Installing vcpkg dependencies for x64-linux using manifest"
  "$MY_VCPKG_ROOT/vcpkg" install --triplet x64-linux --x-manifest-root="$ROOT_DIR"
}

validate_presets() {
  log "Validating CMake presets"
  cmake --list-presets >/dev/null
  
  grep -Eq 'CMAKE_TOOLCHAIN_FILE.*vcpkg.cmake' "$ROOT_DIR/CMakePresets.json" \
    || warn "CMakePresets.json may not configure vcpkg toolchain"
  grep -q '"VCPKG_TARGET_TRIPLET": "x64-linux"' "$ROOT_DIR/CMakePresets.json" \
    || warn "CMakePresets.json may not be using x64-linux for Linux preset"
}

generate_env_file() {
  local env_file="$ROOT_DIR/.env.dev"
  if [[ -f "$env_file" ]]; then
    log ".env.dev already exists"
    return 0
  fi

  log "Creating .env.dev template"
  cat > "$env_file" <<EOF
COMPOSE_PROJECT_NAME=new_server_dev
MSSQL_SA_PASSWORD=Strong!Pass123
MSSQL_PORT=11433
MYSQL_PORT=13306
REDIS_PORT=16379
REDIS_REPLICA_PORT=16380
EOF
}

stack_up() {
  [[ $RUN_STACK -eq 1 ]] || return 0

  local stack_dir=""
  if [[ -d "$ROOT_DIR/dockerredis" ]]; then
    stack_dir="$ROOT_DIR/dockerredis"
  fi

  if [[ -z "$stack_dir" ]]; then
    warn "dockerredis directory not found; skipping stack startup"
    return 0
  fi

  if ! command -v docker >/dev/null 2>&1; then
    warn "docker not found; skipping stack startup"
    return 0
  fi

  if ! docker compose version >/dev/null 2>&1; then
    warn "docker compose plugin not available; skipping stack startup"
    return 0
  fi

  if [[ -x "$stack_dir/init.sh" ]]; then
    log "Starting development stack via dockerredis/init.sh"
    (cd "$stack_dir" && ./init.sh)
  elif [[ -f "$stack_dir/init.sh" ]]; then
    log "Starting development stack via dockerredis/init.sh"
    (cd "$stack_dir" && bash ./init.sh)
  else
    warn "dockerredis/init.sh not found; falling back to docker compose up -d"
    (cd "$stack_dir" && docker compose up -d)
  fi
}

clean_cache() {
  [[ $FORCE_CLEAN -eq 1 ]] || return 0
  log "Removing build_linux cache"
  rm -rf "$ROOT_DIR/build_linux"
}

configure_project() {
  [[ $RUN_CONFIGURE -eq 1 ]] || return 0
  log "Configuring with preset: $PRESET"
  cmake --preset "$PRESET"
}

build_project() {
  [[ $RUN_BUILD -eq 1 ]] || return 0
  log "Building with preset: $BUILD_PRESET"
  if [[ -n "$JOBS" ]]; then
    cmake --build --preset "$BUILD_PRESET" --parallel "$JOBS"
  else
    cmake --build --preset "$BUILD_PRESET"
  fi
}

run_tests_if_present() {
  [[ $RUN_TESTS -eq 1 ]] || return 0

  if [[ ! -d "$BUILD_DIR" ]]; then
    warn "Build directory not found: $BUILD_DIR"
    return 0
  fi

  if [[ -f "$BUILD_DIR/CTestTestfile.cmake" || -f "$BUILD_DIR/DartConfiguration.tcl" ]]; then
    log "Running ctest"
    (cd "$BUILD_DIR" && ctest --output-on-failure)
  else
    warn "CTest files not found; skipping tests"
  fi
}

find_binary() {
  local name="$1"
  find "$BUILD_DIR" -maxdepth 4 -type f \( -name "$name" -o -name "$name.exe" \) 2>/dev/null | head -n 1
}

run_binaries() {
  if [[ $RUN_SERVER -eq 1 ]]; then
    SERVER_BIN=$(find_binary "server")
    [[ -n "$SERVER_BIN" ]] && log "Running server: $SERVER_BIN" && "$SERVER_BIN" || warn "server binary not found"
  fi

  if [[ $RUN_TEST_CLIENT -eq 1 ]]; then
    TEST_CLIENT_BIN=$(find_binary "test_client")
    [[ -n "$TEST_CLIENT_BIN" ]] && log "Running test_client: $TEST_CLIENT_BIN" && "$TEST_CLIENT_BIN" || warn "test_client binary not found"
  fi
}

summary() {
  cat <<EOF

========================================
new_server WSL dev environment complete
========================================
Project root    : $ROOT_DIR
Build type      : $BUILD_TYPE
Configure preset: $PRESET
Build preset    : $BUILD_PRESET

Useful commands:
  cmake --preset $PRESET
  cmake --build --preset $BUILD_PRESET
  ./dev_services.sh up
  ./dev_services.sh ps
  ./dev_services.sh logs
  ./dev_services.sh down

Visual Studio 2022 + WSL:
  1. Open folder in VS
  2. Select preset: $PRESET
  3. Use 'Delete Cache and Reconfigure' if cache is stale
========================================
EOF
}

main() {
  require_cmd bash
  require_cmd git
  require_cmd cmake
  require_cmd grep
  require_cmd sed

  install_base_packages
  install_ms_odbc
  install_docker_if_needed
  ensure_vcpkg_root
  prepare_external_repos
  bootstrap_vcpkg
  install_vcpkg_packages
  validate_presets
  generate_env_file
  stack_up
  clean_cache
  configure_project
  build_project
  run_tests_if_present
  run_binaries
  summary
}

main "$@"
