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

    proc = subprocess.Popen(
        [str(world_bin)],
        cwd=str(repo_root),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    time.sleep(2.0)

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
            print(f"[FAIL] missing shutdown marker: {marker}")
            print("--- captured output (tail) ---")
            print("\n".join(out.splitlines()[-80:]))
            return 1
        pos = nxt

    print("runtime_smoke_world_shutdown passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
