#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys


def _require_in_order(text: str, needles: list[str], label: str) -> bool:
    pos = -1
    for needle in needles:
        nxt = text.find(needle, pos + 1)
        if nxt < 0:
            print(f"[FAIL] {label}: missing '{needle}'")
            return False
        pos = nxt
    return True


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    core_cpp = repo_root / "src/services/world/runtime/world_runtime_core.cpp"
    persistence_cpp = repo_root / "src/services/world/runtime/world_runtime_persistence.cpp"

    core_text = core_cpp.read_text(encoding="utf-8")
    persist_text = persistence_cpp.read_text(encoding="utf-8")

    ok = True

    shutdown_steps = [
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
    ok &= _require_in_order(core_text, shutdown_steps, "shutdown-log-order")

    persistence_needles = [
        "const auto expected_version = TryGetExpectedCharVersion_(world_code, char_id);",
        "actual_version != expected_version",
        "++conflicts;",
        "redis_cache_->mark_dirty(world_code, char_id);",
        "r.conflicts = conflicts;",
    ]
    for needle in persistence_needles:
        if needle not in persist_text:
            print(f"[FAIL] flush-dirty-guard: missing '{needle}'")
            ok = False

    if not ok:
        return 1

    print("smoke_persistence_shutdown passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
