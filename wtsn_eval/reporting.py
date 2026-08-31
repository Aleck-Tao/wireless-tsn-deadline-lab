"""Deterministic cross-seed summaries and paired scheduler comparisons."""

from __future__ import annotations

import csv
import hashlib
import json
import random
import statistics
from collections import defaultdict
from pathlib import Path

from .plan import RunIdentity, expected_run_identities, validate_plan
from .provenance import write_json


def aggregate(artifact_root: Path | str) -> tuple[list[dict[str, object]], dict[str, object]]:
    root = Path(artifact_root)
    plan_path = root / "benchmark_plan.json"
    if not plan_path.is_file():
        raise ValueError("benchmark artifact is missing benchmark_plan.json")
    with plan_path.open(encoding="utf-8") as handle:
        plan = validate_plan(json.load(handle))
    expected = expected_run_identities(plan)
    observed: dict[RunIdentity, Path] = {}
    metrics: list[dict[str, object]] = []
    for path in sorted((root / "runs").rglob("metrics.json")):
        with path.open(encoding="utf-8") as handle:
            row = json.load(handle)
        identity = _metric_identity(row, path)
        if identity in observed:
            raise ValueError(
                f"duplicate run identity {identity}: {observed[identity]} and {path}"
            )
        expected_path = root / "runs" / identity[0] / identity[1] / f"seed-{identity[2]}" / "metrics.json"
        if path.resolve() != expected_path.resolve():
            raise ValueError(f"non-canonical metrics path for run identity {identity}: {path}")
        observed[identity] = path
        metrics.append(row)
    observed_identities = set(observed)
    if observed_identities != expected:
        missing = sorted(expected - observed_identities)
        unexpected = sorted(observed_identities - expected)
        raise ValueError(f"benchmark matrix mismatch; missing={missing}, unexpected={unexpected}")

    grouped: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in metrics:
        grouped[(str(row["scenario_id"]), str(row["scheduler"]))].append(row)
    summary: list[dict[str, object]] = []
    for (scenario, scheduler), rows in sorted(grouped.items()):
        on_time = [float(row["on_time_delivery_ratio"]) for row in rows]
        p99 = [int(row["latency_ns"]["p99"]) for row in rows if row["latency_ns"]["p99"] is not None]
        summary.append(
            {
                "scenario_id": scenario,
                "scheduler": scheduler,
                "runs": len(rows),
                "on_time_ratio_mean": round(statistics.fmean(on_time), 12),
                "on_time_ratio_min": round(min(on_time), 12),
                "latency_p99_ns_mean": round(statistics.fmean(p99), 3) if p99 else None,
                "deadline_misses_total": sum(
                    int(row["generated_packets"]) - int(row["on_time_packets"]) for row in rows
                ),
            }
        )

    comparisons: list[dict[str, object]] = []
    by_scenario: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in metrics:
        by_scenario[str(row["scenario_id"])].append(row)
    for scenario, rows in sorted(by_scenario.items()):
        baseline = {int(row["seed"]): row for row in rows if row["scheduler"] == "FIFO"}
        schedulers = sorted({str(row["scheduler"]) for row in rows} - {"FIFO"})
        for scheduler in schedulers:
            candidate = {int(row["seed"]): row for row in rows if row["scheduler"] == scheduler}
            seeds = sorted(set(baseline) & set(candidate))
            deltas = [
                float(candidate[seed]["on_time_delivery_ratio"])
                - float(baseline[seed]["on_time_delivery_ratio"])
                for seed in seeds
            ]
            low, high = paired_bootstrap_interval(deltas, f"{scenario}:{scheduler}")
            comparisons.append(
                {
                    "scenario_id": scenario,
                    "scheduler": scheduler,
                    "baseline": "FIFO",
                    "paired_seeds": seeds,
                    "on_time_ratio_delta_mean": round(statistics.fmean(deltas), 12),
                    "bootstrap_95_ci": [low, high],
                }
            )

    write_json(root / "summary.json", {"schema_version": "1.0", "groups": summary})
    write_json(root / "paired_comparisons.json", {"schema_version": "1.0", "comparisons": comparisons})
    _write_csv(root / "summary.csv", summary)
    _write_markdown(root / "REPORT.md", summary, comparisons)
    return summary, {"comparisons": comparisons}


def _metric_identity(row: object, path: Path) -> RunIdentity:
    if not isinstance(row, dict):
        raise ValueError(f"metrics file is not a JSON object: {path}")
    scenario = row.get("scenario_id")
    scheduler = row.get("scheduler")
    seed = row.get("seed")
    if not isinstance(scenario, str) or not isinstance(scheduler, str) or type(seed) is not int:
        raise ValueError(f"metrics file has invalid run identity: {path}")
    return scenario, scheduler, seed


def paired_bootstrap_interval(deltas: list[float], identity: str, draws: int = 2000) -> tuple[float, float]:
    if not deltas:
        raise ValueError("paired comparison has no common seeds")
    seed = int.from_bytes(hashlib.sha256(identity.encode("utf-8")).digest()[:8], "big")
    generator = random.Random(seed)
    means = sorted(
        statistics.fmean(generator.choice(deltas) for _ in deltas) for _ in range(draws)
    )
    return round(means[int(0.025 * draws)], 12), round(means[int(0.975 * draws) - 1], 12)


def _write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = list(rows[0])
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def _write_markdown(
    path: Path, summary: list[dict[str, object]], comparisons: list[dict[str, object]]
) -> None:
    lines = [
        "# Benchmark report",
        "",
        "All deadline classifications below are independently recomputed from ground-truth event times.",
        "",
        "| Scenario | Scheduler | Runs | Mean on-time ratio | Mean run p99 latency (ns) | Misses |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for row in summary:
        lines.append(
            f"| {row['scenario_id']} | {row['scheduler']} | {row['runs']} | "
            f"{row['on_time_ratio_mean']} | {row['latency_p99_ns_mean']} | "
            f"{row['deadline_misses_total']} |"
        )
    lines.extend(
        [
            "",
            "## Paired comparisons against FIFO",
            "",
            "Intervals are deterministic percentile bootstrap intervals over paired seed deltas; they are descriptive, not a claim of external validity.",
            "",
            "| Scenario | Scheduler | Paired seeds | Mean on-time delta | Bootstrap 95% CI |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for row in comparisons:
        lines.append(
            f"| {row['scenario_id']} | {row['scheduler']} | {len(row['paired_seeds'])} | "
            f"{row['on_time_ratio_delta_mean']} | {row['bootstrap_95_ci']} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
