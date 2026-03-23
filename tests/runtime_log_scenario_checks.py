#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def require_in_order(text: str, needles: list[str], label: str) -> bool:
    pos = -1
    for needle in needles:
        nxt = text.find(needle, pos + 1)
        if nxt < 0:
            print(f"[FAIL] {label}: missing '{needle}'")
            return False
        pos = nxt
    return True


def require_regex(text: str, pattern: str, label: str) -> bool:
    if not re.search(pattern, text, flags=re.MULTILINE):
        print(f"[FAIL] {label}: regex not found: {pattern}")
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Runtime log scenario checker (reconnect/dup/shutdown/auth)")
    ap.add_argument("--log", required=True, help="Path to runtime log file")
    args = ap.parse_args()

    log_path = Path(args.log)
    if not log_path.exists():
        print(f"[FAIL] missing log file: {log_path}")
        return 2

    text = log_path.read_text(encoding="utf-8", errors="ignore")
    ok = True

    # A/D/E 런타임 체크를 로그 기반으로 자동화
    ok &= require_regex(
        text,
        r"\[session_close\] reconnect grace close armed\. char_id=\d+ sid=\d+ serial=\d+ delay_ms=\d+",
        "reconnect-grace-armed")
    ok &= require_regex(
        text,
        r"\[dupstats\] char/s=\d+ account/s=\d+ both/s=\d+ dedup_same/s=\d+",
        "dupstats-shape")
    ok &= require_regex(
        text,
        r"\[dupstats\] char/s=(?!0\b)\d+|"
        r"\[dupstats\] .*account/s=(?!0\b)\d+|"
        r"\[dupstats\] .*both/s=(?!0\b)\d+|"
        r"\[dupstats\] .*dedup_same/s=(?!0\b)\d+",
        "dupstats-positive-signal")
    ok &= require_regex(
        text,
        r"\[authstats\] unauth_packet_rejects/s=\d+ threshold=\d+ sampled_sid=\d+",
        "authstats-shape")
    ok &= require_regex(
        text,
        r"\[FlushOneCharConflict\] world=\d+ char_id=\d+ expected_ver=\d+ actual_ver=\d+",
        "flush-one-conflict-shape")
    ok &= require_regex(
        text,
        r"\[FlushDirtyCharsConflict\] world=\d+ shard=\d+ char_id=\d+ expected_ver=\d+ actual_ver=\d+",
        "flush-dirty-conflict-shape")

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
    ok &= require_in_order(text, shutdown_steps, "shutdown-order-runtime")

    if not ok:
        return 1

    print("runtime_log_scenario_checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
