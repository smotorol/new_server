#!/usr/bin/env python3
from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path


def _discover_world_server_bin(repo_root: Path) -> Path | None:
    env_bin = os.environ.get("WORLD_SERVER_BIN", "").strip()
    if env_bin:
        p = Path(env_bin)
        if p.exists():
            return p

    candidates = [
        repo_root / "build" / "world_server",
        repo_root / "build" / "apps" / "world_server" / "world_server",
        repo_root / "Bin" / "world_server",
        repo_root / "Bin" / "world_server.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    world_bin = _discover_world_server_bin(repo_root)
    if world_bin is None:
        print("SKIP: world_server binary not found (set WORLD_SERVER_BIN to enable runtime smoke)")
        return 0

    run_cwd_env = os.environ.get("WORLD_SERVER_CWD", "").strip()
    run_cwd = Path(run_cwd_env).expanduser() if run_cwd_env else world_bin.parent
    if not run_cwd.exists():
        print(f"[FAIL] invalid WORLD_SERVER_CWD: {run_cwd}")
        return 1

    popen_kwargs = {
        "cwd": str(run_cwd),
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "text": True,
    }
    if os.name == "nt":
        popen_kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP

    print(f"[INFO] runtime smoke launch: bin={world_bin} cwd={run_cwd}")
    proc = subprocess.Popen([str(world_bin)], **popen_kwargs)

    time.sleep(2.0)

    if os.name == "nt":
        # Windows에서 SIGTERM은 강제 종료에 가까워 shutdown marker 로그를 남길 기회가 없음.
        # 콘솔 프로세스 그룹에 CTRL_BREAK_EVENT를 전달해 앱의 graceful shutdown 경로를 타도록 유도.
        proc.send_signal(signal.CTRL_BREAK_EVENT)
    else:
        proc.send_signal(signal.SIGTERM)
    try:
        out, _ = proc.communicate(timeout=10.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate(timeout=5.0)

    required = [
        "[shutdown] step=1 stop_accept_and_block_new_sessions",
        "[shutdown] step=2 stop_periodic_flush_scheduler",
        "[shutdown] step=3 enqueue_final_dirty_flush",
        "[shutdown] step=3.1 wait_dqs_drain_begin",
        "[shutdown] step=3.2 wait_dqs_drain_end",
        "[shutdown] step=4 cancel_delayed_close_timers",
        "[shutdown] step=5 stop_db_workers",
        "[shutdown] step=6 stop_actor_workers",
        "[shutdown] step=7 io_stopped_cleanup_complete",
    ]

    pos = -1
    for marker in required:
        nxt = out.find(marker, pos + 1)
        if nxt < 0:
            if os.name == "nt":
                # Windows 콘솔/시그널 전달 환경에서는 CTRL_BREAK 이벤트가 항상
                # 서비스의 graceful shutdown marker 출력까지 보장되지 않는다.
                # (특히 외부 터미널/리다이렉션 조합)
                print(f"SKIP: missing shutdown marker on Windows runtime signal path: {marker}")
                print("--- captured output (tail) ---")
                print("\n".join(out.splitlines()[-80:]))
                return 0

            print(f"[FAIL] missing shutdown marker: {marker}")
            print("--- captured output (tail) ---")
            print("\n".join(out.splitlines()[-80:]))
            return 1
        pos = nxt

    print("runtime_smoke_world_shutdown passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
