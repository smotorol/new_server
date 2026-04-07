#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PERF_PATTERN = re.compile(r"aoi_perf_zone_(\d+)\.csv$", re.IGNORECASE)
SUMMARY_PATTERN = re.compile(r"aoi_summary_zone_(\d+)\.csv$", re.IGNORECASE)
HOTSPOT_PATTERN = re.compile(r"aoi_hotspot_zone_(\d+)\.csv$", re.IGNORECASE)


@dataclass
class PerfRow:
    zone_server_id: int
    timestamp_ms: int
    map_key: int
    actor_id: int
    candidate_count: int
    visible_before: int
    visible_after: int
    occupied_cells: int
    neighbor_cells: int
    visible_calc_time_us: int
    diff_calc_time_us: int
    spawn_build_time_us: int
    despawn_build_time_us: int
    move_build_time_us: int
    total_move_time_us: int


@dataclass
class SummaryRow:
    zone_server_id: int
    timestamp_ms: int
    map_key: int
    moves: int
    avg_total_move_us: int
    p95_total_move_us: int
    max_total_move_us: int
    avg_candidate_count: int
    max_candidate_count: int
    actor_count: int


@dataclass
class HotspotRow:
    zone_server_id: int
    timestamp_ms: int
    map_key: int
    rank: int
    cell_key: int
    cell_x: int
    cell_y: int
    actor_count: int
    occupied_cells: int


def percentile(values: list[int], p: float) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    index = math.ceil((p / 100.0) * len(ordered)) - 1
    index = max(0, min(index, len(ordered) - 1))
    return ordered[index]


def avg(values: Iterable[int]) -> float:
    items = list(values)
    return float(sum(items)) / float(len(items)) if items else 0.0


def parse_int(row: dict[str, str], key: str) -> int:
    value = row.get(key, "")
    if value == "":
        return 0
    return int(value)


def decode_map_key(map_key: int) -> tuple[int, int]:
    return ((map_key >> 32) & 0xFFFFFFFF, map_key & 0xFFFFFFFF)


def format_map_key(map_key: int) -> str:
    map_template_id, instance_id = decode_map_key(map_key)
    return f"{map_key} (map={map_template_id},instance={instance_id})"


def format_ts(timestamp_ms: int) -> str:
    return datetime.fromtimestamp(timestamp_ms / 1000.0, tz=timezone.utc).astimezone().strftime("%Y-%m-%d %H:%M:%S")


def load_perf_rows(root: Path, min_timestamp_ms: int | None) -> list[PerfRow]:
    rows: list[PerfRow] = []
    for path in sorted(root.glob("aoi_perf_zone_*.csv")):
        match = PERF_PATTERN.search(path.name)
        if not match:
            continue
        zone_server_id = int(match.group(1))
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                timestamp_ms = parse_int(row, "timestamp_ms")
                if min_timestamp_ms is not None and timestamp_ms < min_timestamp_ms:
                    continue
                rows.append(
                    PerfRow(
                        zone_server_id=zone_server_id,
                        timestamp_ms=timestamp_ms,
                        map_key=parse_int(row, "map_key"),
                        actor_id=parse_int(row, "actor_id"),
                        candidate_count=parse_int(row, "candidate_count"),
                        visible_before=parse_int(row, "visible_before"),
                        visible_after=parse_int(row, "visible_after"),
                        occupied_cells=parse_int(row, "occupied_cells"),
                        neighbor_cells=parse_int(row, "neighbor_cells"),
                        visible_calc_time_us=parse_int(row, "visible_calc_time_us"),
                        diff_calc_time_us=parse_int(row, "diff_calc_time_us"),
                        spawn_build_time_us=parse_int(row, "spawn_build_time_us"),
                        despawn_build_time_us=parse_int(row, "despawn_build_time_us"),
                        move_build_time_us=parse_int(row, "move_build_time_us"),
                        total_move_time_us=parse_int(row, "total_move_time_us"),
                    )
                )
    return rows


def load_summary_rows(root: Path, min_timestamp_ms: int | None) -> list[SummaryRow]:
    rows: list[SummaryRow] = []
    for path in sorted(root.glob("aoi_summary_zone_*.csv")):
        match = SUMMARY_PATTERN.search(path.name)
        if not match:
            continue
        zone_server_id = int(match.group(1))
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                timestamp_ms = parse_int(row, "timestamp_ms")
                if min_timestamp_ms is not None and timestamp_ms < min_timestamp_ms:
                    continue
                rows.append(
                    SummaryRow(
                        zone_server_id=zone_server_id,
                        timestamp_ms=timestamp_ms,
                        map_key=parse_int(row, "map_key"),
                        moves=parse_int(row, "moves"),
                        avg_total_move_us=parse_int(row, "avg_total_move_us"),
                        p95_total_move_us=parse_int(row, "p95_total_move_us"),
                        max_total_move_us=parse_int(row, "max_total_move_us"),
                        avg_candidate_count=parse_int(row, "avg_candidate_count"),
                        max_candidate_count=parse_int(row, "max_candidate_count"),
                        actor_count=parse_int(row, "actor_count"),
                    )
                )
    return rows


def load_hotspot_rows(root: Path, min_timestamp_ms: int | None) -> list[HotspotRow]:
    rows: list[HotspotRow] = []
    for path in sorted(root.glob("aoi_hotspot_zone_*.csv")):
        match = HOTSPOT_PATTERN.search(path.name)
        if not match:
            continue
        zone_server_id = int(match.group(1))
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                timestamp_ms = parse_int(row, "timestamp_ms")
                if min_timestamp_ms is not None and timestamp_ms < min_timestamp_ms:
                    continue
                rows.append(
                    HotspotRow(
                        zone_server_id=zone_server_id,
                        timestamp_ms=timestamp_ms,
                        map_key=parse_int(row, "map_key"),
                        rank=parse_int(row, "rank"),
                        cell_key=parse_int(row, "cell_key"),
                        cell_x=parse_int(row, "cell_x"),
                        cell_y=parse_int(row, "cell_y"),
                        actor_count=parse_int(row, "actor_count"),
                        occupied_cells=parse_int(row, "occupied_cells"),
                    )
                )
    return rows


def analyze_perf(rows: list[PerfRow], candidate_threshold: int, top_n: int) -> dict[str, Any]:
    map_groups: dict[tuple[int, int], list[PerfRow]] = defaultdict(list)
    for row in rows:
        map_groups[(row.zone_server_id, row.map_key)].append(row)

    per_map: list[dict[str, Any]] = []
    for (zone_server_id, map_key), items in sorted(map_groups.items()):
        total_moves = [r.total_move_time_us for r in items]
        candidates = [r.candidate_count for r in items]
        visible_after = [r.visible_after for r in items]
        entry = {
            "zone_server_id": zone_server_id,
            "map_key": map_key,
            "map_template_id": decode_map_key(map_key)[0],
            "instance_id": decode_map_key(map_key)[1],
            "sample_count": len(items),
            "avg_total_move_us": round(avg(total_moves), 2),
            "p95_total_move_us": percentile(total_moves, 95),
            "max_total_move_us": max(total_moves) if total_moves else 0,
            "avg_candidate_count": round(avg(candidates), 2),
            "max_candidate_count": max(candidates) if candidates else 0,
            "avg_visible_after": round(avg(visible_after), 2),
            "max_visible_after": max(visible_after) if visible_after else 0,
            "candidate_threshold_exceeded": sum(1 for x in candidates if x >= candidate_threshold),
        }
        avg_move = entry["avg_total_move_us"] or 1.0
        entry["worst_ratio_vs_avg"] = round(float(entry["max_total_move_us"]) / float(avg_move), 2)
        per_map.append(entry)

    top_slowest = sorted(rows, key=lambda r: (r.total_move_time_us, r.candidate_count), reverse=True)[:top_n]
    candidate_spikes = sorted(rows, key=lambda r: (r.candidate_count, r.total_move_time_us), reverse=True)[:top_n]
    worst_map = max(per_map, key=lambda x: (x["p95_total_move_us"], x["max_total_move_us"]), default=None)

    return {
        "per_map": per_map,
        "top_slowest_moves": [
            {
                "zone_server_id": r.zone_server_id,
                "timestamp_ms": r.timestamp_ms,
                "map_key": r.map_key,
                "map_template_id": decode_map_key(r.map_key)[0],
                "instance_id": decode_map_key(r.map_key)[1],
                "actor_id": r.actor_id,
                "candidate_count": r.candidate_count,
                "visible_after": r.visible_after,
                "total_move_time_us": r.total_move_time_us,
            }
            for r in top_slowest
        ],
        "top_candidate_spikes": [
            {
                "zone_server_id": r.zone_server_id,
                "timestamp_ms": r.timestamp_ms,
                "map_key": r.map_key,
                "map_template_id": decode_map_key(r.map_key)[0],
                "instance_id": decode_map_key(r.map_key)[1],
                "actor_id": r.actor_id,
                "candidate_count": r.candidate_count,
                "total_move_time_us": r.total_move_time_us,
            }
            for r in candidate_spikes
        ],
        "worst_map_by_p95": worst_map,
    }


def analyze_hotspots(rows: list[HotspotRow], top_n: int) -> dict[str, Any]:
    grouped: dict[tuple[int, int, int], list[HotspotRow]] = defaultdict(list)
    for row in rows:
        grouped[(row.zone_server_id, row.map_key, row.cell_key)].append(row)

    repeated: list[dict[str, Any]] = []
    for (zone_server_id, map_key, cell_key), items in grouped.items():
        repeated.append(
            {
                "zone_server_id": zone_server_id,
                "map_key": map_key,
                "map_template_id": decode_map_key(map_key)[0],
                "instance_id": decode_map_key(map_key)[1],
                "cell_key": cell_key,
                "cell_x": items[0].cell_x,
                "cell_y": items[0].cell_y,
                "appearances": len(items),
                "max_actor_count": max(i.actor_count for i in items),
                "avg_actor_count": round(avg(i.actor_count for i in items), 2),
            }
        )
    repeated.sort(key=lambda x: (x["appearances"], x["max_actor_count"]), reverse=True)

    per_map_top: dict[str, list[dict[str, Any]]] = {}
    map_groups: dict[tuple[int, int], list[HotspotRow]] = defaultdict(list)
    for row in rows:
        map_groups[(row.zone_server_id, row.map_key)].append(row)
    for (zone_server_id, map_key), items in sorted(map_groups.items()):
        ranked = sorted(items, key=lambda x: (x.actor_count, -x.rank), reverse=True)[:top_n]
        per_map_top[f"{zone_server_id}:{map_key}"] = [
            {
                "zone_server_id": row.zone_server_id,
                "map_key": row.map_key,
                "map_template_id": decode_map_key(row.map_key)[0],
                "instance_id": decode_map_key(row.map_key)[1],
                "cell_key": row.cell_key,
                "cell_x": row.cell_x,
                "cell_y": row.cell_y,
                "actor_count": row.actor_count,
                "rank": row.rank,
                "timestamp_ms": row.timestamp_ms,
            }
            for row in ranked
        ]

    return {
        "repeated_hotspots": repeated[:top_n],
        "per_map_top_hotspots": per_map_top,
        "worst_hotspot": repeated[0] if repeated else None,
    }


def analyze_summary(rows: list[SummaryRow]) -> dict[str, Any]:
    latest_by_map: dict[tuple[int, int], SummaryRow] = {}
    for row in rows:
        key = (row.zone_server_id, row.map_key)
        if key not in latest_by_map or row.timestamp_ms > latest_by_map[key].timestamp_ms:
            latest_by_map[key] = row
    latest = []
    for (zone_server_id, map_key), row in sorted(latest_by_map.items()):
        latest.append(
            {
                "zone_server_id": zone_server_id,
                "map_key": map_key,
                "map_template_id": decode_map_key(map_key)[0],
                "instance_id": decode_map_key(map_key)[1],
                "timestamp_ms": row.timestamp_ms,
                "moves": row.moves,
                "avg_total_move_us": row.avg_total_move_us,
                "p95_total_move_us": row.p95_total_move_us,
                "max_total_move_us": row.max_total_move_us,
                "avg_candidate_count": row.avg_candidate_count,
                "max_candidate_count": row.max_candidate_count,
                "actor_count": row.actor_count,
            }
        )
    return {"latest_summary_rows": latest}


def build_text_report(analysis: dict[str, Any], top_n: int, root: Path, minutes: int | None) -> str:
    lines: list[str] = []
    lines.append("AOI Analysis Report")
    lines.append(f"Input Directory: {root}")
    lines.append(f"Time Filter: recent {minutes} minute(s)" if minutes is not None else "Time Filter: all data")
    lines.append("")

    perf = analysis["perf"]
    hotspot = analysis["hotspot"]
    summary = analysis["summary"]

    lines.append("Per-Map Performance")
    if perf["per_map"]:
        header = f"{'Zone':>4} {'MapKey':>28} {'AvgMove':>10} {'P95':>8} {'Max':>8} {'AvgCand':>10} {'MaxCand':>8} {'AvgVis':>8} {'MaxVis':>8}"
        lines.append(header)
        lines.append("-" * len(header))
        for item in perf["per_map"]:
            lines.append(
                f"{item['zone_server_id']:>4} {format_map_key(item['map_key']):>28} "
                f"{item['avg_total_move_us']:>10} {item['p95_total_move_us']:>8} {item['max_total_move_us']:>8} "
                f"{item['avg_candidate_count']:>10} {item['max_candidate_count']:>8} "
                f"{item['avg_visible_after']:>8} {item['max_visible_after']:>8}"
            )
    else:
        lines.append("No perf rows.")
    lines.append("")

    lines.append(f"Top {top_n} Slowest Moves")
    for item in perf["top_slowest_moves"]:
        lines.append(
            f"- zone={item['zone_server_id']} map={format_map_key(item['map_key'])} actor={item['actor_id']} "
            f"time_us={item['total_move_time_us']} candidates={item['candidate_count']} visible_after={item['visible_after']} "
            f"ts={format_ts(item['timestamp_ms'])}"
        )
    if not perf["top_slowest_moves"]:
        lines.append("No slow move rows.")
    lines.append("")

    lines.append(f"Top {top_n} Candidate Spikes")
    for item in perf["top_candidate_spikes"]:
        lines.append(
            f"- zone={item['zone_server_id']} map={format_map_key(item['map_key'])} actor={item['actor_id']} "
            f"candidate_count={item['candidate_count']} total_move_time_us={item['total_move_time_us']} ts={format_ts(item['timestamp_ms'])}"
        )
    if not perf["top_candidate_spikes"]:
        lines.append("No candidate spike rows.")
    lines.append("")

    lines.append(f"Top {top_n} Repeated Hotspot Cells")
    for item in hotspot["repeated_hotspots"]:
        lines.append(
            f"- zone={item['zone_server_id']} map={format_map_key(item['map_key'])} "
            f"cell=({item['cell_x']},{item['cell_y']}) appearances={item['appearances']} "
            f"max_actor_count={item['max_actor_count']} avg_actor_count={item['avg_actor_count']}"
        )
    if not hotspot["repeated_hotspots"]:
        lines.append("No hotspot rows.")
    lines.append("")

    lines.append("Latest Summary Rows")
    for item in summary["latest_summary_rows"][:top_n]:
        lines.append(
            f"- zone={item['zone_server_id']} map={format_map_key(item['map_key'])} "
            f"avg_total_move_us={item['avg_total_move_us']} p95_total_move_us={item['p95_total_move_us']} "
            f"max_total_move_us={item['max_total_move_us']} actor_count={item['actor_count']} ts={format_ts(item['timestamp_ms'])}"
        )
    if not summary["latest_summary_rows"]:
        lines.append("No summary rows.")
    lines.append("")

    lines.append("Overall Summary")
    worst_map = perf["worst_map_by_p95"]
    if worst_map:
        lines.append(
            f"- Slowest map by p95: zone={worst_map['zone_server_id']} map={format_map_key(worst_map['map_key'])} "
            f"p95_total_move_us={worst_map['p95_total_move_us']} max_total_move_us={worst_map['max_total_move_us']} "
            f"worst_ratio_vs_avg={worst_map['worst_ratio_vs_avg']}"
        )
    worst_hotspot = hotspot["worst_hotspot"]
    if worst_hotspot:
        lines.append(
            f"- Strongest repeated hotspot: zone={worst_hotspot['zone_server_id']} map={format_map_key(worst_hotspot['map_key'])} "
            f"cell=({worst_hotspot['cell_x']},{worst_hotspot['cell_y']}) appearances={worst_hotspot['appearances']} "
            f"max_actor_count={worst_hotspot['max_actor_count']}"
        )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze AOI CSV logs from new_server zone runtime.")
    parser.add_argument("input_dir", type=Path, help="Directory containing aoi_perf_zone_*.csv and related files")
    parser.add_argument("--minutes", type=int, default=None, help="Only analyze rows newer than the last N minutes")
    parser.add_argument("--top", type=int, default=10, help="Top N rows/cells to print")
    parser.add_argument("--candidate-threshold", type=int, default=128, help="Candidate count threshold for spike counting")
    parser.add_argument("--report-prefix", type=str, default="aoi_analysis_report", help="Output report file prefix")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.input_dir
    if not root.exists() or not root.is_dir():
        print(f"Input directory not found: {root}", file=sys.stderr)
        return 1

    min_timestamp_ms = None
    if args.minutes is not None:
        now_ms = int(datetime.now(tz=timezone.utc).timestamp() * 1000)
        min_timestamp_ms = now_ms - (args.minutes * 60 * 1000)

    perf_rows = load_perf_rows(root, min_timestamp_ms)
    summary_rows = load_summary_rows(root, min_timestamp_ms)
    hotspot_rows = load_hotspot_rows(root, min_timestamp_ms)

    if not perf_rows and not summary_rows and not hotspot_rows:
        print(f"No AOI CSV files found in {root}", file=sys.stderr)
        return 2

    analysis = {
        "meta": {
            "input_dir": str(root),
            "minutes": args.minutes,
            "top": args.top,
            "candidate_threshold": args.candidate_threshold,
            "perf_rows": len(perf_rows),
            "summary_rows": len(summary_rows),
            "hotspot_rows": len(hotspot_rows),
        },
        "perf": analyze_perf(perf_rows, args.candidate_threshold, args.top),
        "summary": analyze_summary(summary_rows),
        "hotspot": analyze_hotspots(hotspot_rows, args.top),
    }

    text_report = build_text_report(analysis, args.top, root, args.minutes)
    print(text_report, end="")

    report_txt = root / f"{args.report_prefix}.txt"
    report_json = root / f"{args.report_prefix}.json"
    report_txt.write_text(text_report, encoding="utf-8")
    report_json.write_text(json.dumps(analysis, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"\nWrote: {report_txt}")
    print(f"Wrote: {report_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
