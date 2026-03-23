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


def require_absent_regex(text: str, pattern: str, label: str) -> bool:
    if re.search(pattern, text, flags=re.MULTILINE):
        print(f"[FAIL] {label}: unexpected regex found: {pattern}")
        return False
    return True


def require_numeric_relation_from_log(
    text: str,
    pattern: str,
    label: str,
    relation: str,
) -> bool:
    m = re.search(pattern, text, flags=re.MULTILINE)
    if not m:
        print(f"[FAIL] {label}: regex not found: {pattern}")
        return False
    lhs = int(m.group(1))
    rhs = int(m.group(2))
    if relation == "gt" and lhs <= rhs:
        print(f"[FAIL] {label}: expected {lhs} > {rhs}")
        return False
    if relation == "eq" and lhs != rhs:
        print(f"[FAIL] {label}: expected {lhs} == {rhs}")
        return False
    return True


def run_common_checks(text: str) -> bool:
    ok = True
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
        r"\[FlushOneChar\] world=\d+ char_id=\d+ saved=(?:0|1) result=\d+ expected_ver=\d+ actual_ver=\d+",
        "flush-one-shape")
    ok &= require_regex(
        text,
        r"\[FlushOneCharConflict\] world=\d+ char_id=\d+ expected_ver=\d+ actual_ver=\d+",
        "flush-one-conflict-shape")
    ok &= require_regex(
        text,
        r"\[FlushDirtyChars\] world=\d+ shard=\d+ pulled=\d+ saved=\d+ failed=\d+ conflicts=\d+ batch=\d+ result=\d+",
        "flush-dirty-summary-shape")
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
    return ok


def run_reconnect_within_grace_checks(text: str) -> bool:
    ok = True
    steps = [
        "[session_close] reconnect grace close armed.",
        "[session_close] delayed close canceled/released",
    ]
    ok &= require_in_order(text, steps, "reconnect-within-grace-order")
    ok &= require_absent_regex(
        text,
        r"\[session_close\] delayed close fired char_id=\d+ sid=\d+ serial=\d+",
        "reconnect-within-grace-no-fired")
    return ok


def run_reconnect_after_grace_checks(text: str) -> bool:
    ok = True
    steps = [
        "[session_close] reconnect grace close armed.",
        "[session_close] delayed close fired",
        "[session_close] world close post-processing completed on normal path",
    ]
    ok &= require_in_order(text, steps, "reconnect-after-grace-order")
    return ok


def run_dup_category_checks(text: str) -> bool:
    return require_regex(
        text,
        r"\[dupstats\] char/s=(?!0\b)\d+ account/s=(?!0\b)\d+ both/s=(?!0\b)\d+ dedup_same/s=(?!0\b)\d+",
        "dupstats-all-categories-positive")


def run_auth_threshold_checks(text: str) -> bool:
    return require_numeric_relation_from_log(
        text,
        r"\[authstats\] unauth_packet_rejects/s=(\d+) threshold=(\d+) sampled_sid=\d+",
        "authstats-threshold-exceeded",
        relation="gt")


def run_flush_success_conflict_checks(text: str) -> bool:
    ok = True
    ok &= require_regex(
        text,
        r"\[FlushOneChar\] world=\d+ char_id=\d+ saved=1 result=\d+ expected_ver=(\d+) actual_ver=\1",
        "flush-one-success")
    ok &= require_regex(
        text,
        r"\[FlushOneCharConflict\] world=\d+ char_id=\d+ expected_ver=\d+ actual_ver=\d+",
        "flush-one-conflict")
    return ok


def run_shutdown_timeout_checks(text: str) -> bool:
    ok = True
    ok &= require_regex(
        text,
        r"\[shutdown\] step=3\.2 wait_dqs_drain_end in_flight=\d+ timed_out=1",
        "shutdown-timeout-flag")
    ok &= require_regex(
        text,
        r"\[shutdown\] dqs drain timed out\. in_flight=\d+ \(continuing shutdown with forced worker stop\)",
        "shutdown-timeout-warning")
    ok &= require_in_order(
        text,
        [
            "[shutdown] step=3.2 wait_dqs_drain_end",
            "[shutdown] dqs drain timed out.",
            "[shutdown] step=4 cancel_delayed_close_timers",
            "[shutdown] step=5 stop_db_workers",
            "[shutdown] step=6 stop_actor_workers",
            "[shutdown] step=7 io_stopped_cleanup_complete",
        ],
        "shutdown-timeout-order")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Runtime log scenario checker (reconnect/dup/shutdown/auth)")
    ap.add_argument("--log", required=True, help="Path to runtime log file")
    ap.add_argument(
        "--profile",
        choices=[
            "full",
            "reconnect_within_grace",
            "reconnect_after_grace",
            "dup_categories",
            "auth_threshold_exceeded",
            "flush_success_conflict",
            "shutdown_timeout",
        ],
        default="full",
        help="Scenario profile to validate")
    args = ap.parse_args()

    log_path = Path(args.log)
    if not log_path.exists():
        print(f"[FAIL] missing log file: {log_path}")
        return 2

    text = log_path.read_text(encoding="utf-8", errors="ignore")
    ok = run_common_checks(text)
    if args.profile == "reconnect_within_grace":
        ok &= run_reconnect_within_grace_checks(text)
    elif args.profile == "reconnect_after_grace":
        ok &= run_reconnect_after_grace_checks(text)
    elif args.profile == "dup_categories":
        ok &= run_dup_category_checks(text)
    elif args.profile == "auth_threshold_exceeded":
        ok &= run_auth_threshold_checks(text)
    elif args.profile == "flush_success_conflict":
        ok &= run_flush_success_conflict_checks(text)
    elif args.profile == "shutdown_timeout":
        ok &= run_shutdown_timeout_checks(text)

    if not ok:
        return 1

    print(f"runtime_log_scenario_checks passed (profile={args.profile})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
