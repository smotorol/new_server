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
    runtime_network_cpp = repo_root / "src/services/world/runtime/world_runtime_network.cpp"
    runtime_h = repo_root / "src/services/world/runtime/world_runtime.h"
    persistence_cpp = repo_root / "src/services/world/runtime/world_runtime_persistence.cpp"
    handler_core_cpp = repo_root / "src/services/world/handler/world_handler_core.cpp"
    session_cpp = repo_root / "src/services/world/runtime/world_runtime_session.cpp"

    core_text = core_cpp.read_text(encoding="utf-8")
    runtime_network_text = runtime_network_cpp.read_text(encoding="utf-8")
    runtime_h_text = runtime_h.read_text(encoding="utf-8")
    persist_text = persistence_cpp.read_text(encoding="utf-8")
    handler_text = handler_core_cpp.read_text(encoding="utf-8")
    session_text = session_cpp.read_text(encoding="utf-8")

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

    auth_needles_core = [
        "g_world_unauth_packet_rejects.fetch_add(1, std::memory_order_relaxed);",
        "g_world_unauth_last_sid.store(sid, std::memory_order_relaxed);",
        "[auth] rejected unauthenticated world packet",
    ]
    for needle in auth_needles_core:
        if needle not in handler_text:
            print(f"[FAIL] auth-guard: missing '{needle}'")
            ok = False

    auth_needles_stats = [
        "kUnauthWarnThresholdPerSec = 10",
        "[authstats] unauth_packet_rejects/s={} threshold={} sampled_sid={}",
    ]
    for needle in auth_needles_stats:
        if needle not in core_text:
            print(f"[FAIL] authstats-threshold: missing '{needle}'")
            ok = False

    shutdown_drain_needles = [
        "std::size_t last_in_flight = CountInFlightDqs_();",
        "last_in_flight = CountInFlightDqs_();",
        "[shutdown] step=3.2 wait_dqs_drain_end in_flight={} timed_out={}",
        "[shutdown] dqs drain timed out. in_flight={} (continuing shutdown with forced worker stop)",
    ]
    for needle in shutdown_drain_needles:
        if needle not in core_text:
            print(f"[FAIL] shutdown-drain: missing '{needle}'")
            ok = False

    conflict_log_needles = [
        "[FlushOneCharConflict] world={} char_id={} expected_ver={} actual_ver={}",
        "[FlushDirtyCharsConflict] world={} shard={} char_id={} expected_ver={} actual_ver={}",
    ]
    for needle in conflict_log_needles:
        if needle not in persist_text:
            print(f"[FAIL] conflict-log-shape: missing '{needle}'")
            ok = False

    reconnect_needles = [
        "reconnect_grace_close_delay_ms_ = 5000",
        "RECONNECT_GRACE_CLOSE_DELAY_MS",
        "TryReserveDelayedWorldClose_(sid, serial)",
        "ArmReservedDelayedWorldClose_(",
        "[session_close] reconnect grace close armed. char_id={} sid={} serial={} delay_ms={}",
    ]
    reconnect_sources = [runtime_h_text, runtime_network_text, session_text]
    for needle in reconnect_needles:
        if not any(needle in src for src in reconnect_sources):
            print(f"[FAIL] reconnect-grace: missing '{needle}'")
            ok = False

    dup_needles_session = [
        "g_dup_login_char.fetch_add(1, std::memory_order_relaxed);",
        "g_dup_login_account.fetch_add(1, std::memory_order_relaxed);",
        "g_dup_login_both.fetch_add(1, std::memory_order_relaxed);",
        "g_dup_login_dedup_same_session.fetch_add(1, std::memory_order_relaxed);",
    ]
    for needle in dup_needles_session:
        if needle not in session_text:
            print(f"[FAIL] dup-metrics-session: missing '{needle}'")
            ok = False

    dup_needles_stats = [
        "[dupstats] char/s={} account/s={} both/s={} dedup_same/s={}",
    ]
    for needle in dup_needles_stats:
        if needle not in core_text:
            print(f"[FAIL] dupstats-log: missing '{needle}'")
            ok = False

    if not ok:
        return 1

    print("smoke_persistence_shutdown passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
