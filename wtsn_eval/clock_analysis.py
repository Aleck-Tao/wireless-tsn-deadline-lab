"""Paired factor contrasts for the clock estimator experiment."""

from __future__ import annotations

import json
import statistics
from pathlib import Path

from .provenance import write_json
from .reporting import paired_bootstrap_interval


def paired_contrast(baseline: dict[int, float], candidate: dict[int, float], label: str) -> dict:
    if not baseline or set(baseline) != set(candidate):
        raise ValueError("clock contrast requires exactly matching non-empty seed sets")
    deltas = {seed: candidate[seed] - baseline[seed] for seed in sorted(baseline)}
    return {
        "contrast": label,
        "paired_seed_deltas": deltas,
        "mean_on_time_delta": statistics.fmean(deltas.values()),
        "bootstrap_95_ci": paired_bootstrap_interval(list(deltas.values()), label),
    }


def write_clock_analysis(root: Path) -> None:
    groups: dict[tuple[int, str, bool], dict[int, dict]] = {}
    raw_edf: dict[tuple[int, int], list[float]] = {}
    for path in sorted((root / "runs").rglob("metrics.json")):
        metrics = json.loads(path.read_text(encoding="utf-8"))
        metadata = json.loads((path.parent / "run_metadata.json").read_text(encoding="utf-8"))
        if not metadata["independent_clock_noise"]:
            raise ValueError("clock ablation requires independent measurement/timestamp noise streams")
        noise, seed = metadata["sync_measurement_noise_ns"], metadata["seed"]
        if metrics["scheduler"] == "EDF":
            raw_edf.setdefault((noise, seed), []).append(metrics["on_time_delivery_ratio"])
            continue
        key = (noise, metadata["sync_estimator"], metadata["clock_margin_enabled"])
        group = groups.setdefault(key, {})
        if seed in group:
            raise ValueError("duplicate clock factor/seed combination")
        group[seed] = metrics
    if any(len(values) != 4 or len(set(values)) != 1 for values in raw_edf.values()):
        raise ValueError("raw EDF control differs across estimator/margin treatments")
    rows = []
    for (noise, estimator, margin), seeds in sorted(groups.items()):
        values = list(seeds.values())
        rows.append({
            "noise_ns": noise, "estimator": estimator, "margin_enabled": margin,
            "seeds": sorted(seeds),
            "on_time_ratio_mean": statistics.fmean(r["on_time_delivery_ratio"] for r in values),
            "outage_p99_error_ns_mean": statistics.fmean(
                r["sync_outage_holdover_absolute_error_ns"]["p99"] for r in values),
            "heuristic_margin_coverage_mean": statistics.fmean(
                r["sync_outage_heuristic_margin_coverage"] for r in values),
        })
    contrasts = []
    def ratios(key: tuple) -> dict[int, float]:
        return {seed: row["on_time_delivery_ratio"] for seed, row in groups[key].items()}
    for noise in sorted({key[0] for key in groups}):
        for margin in (False, True):
            contrasts.append(paired_contrast(
                ratios((noise, "two_point", margin)), ratios((noise, "window_ols", margin)),
                f"noise={noise};margin={margin};window_ols minus two_point"))
        for estimator in ("two_point", "window_ols"):
            contrasts.append(paired_contrast(
                ratios((noise, estimator, False)), ratios((noise, estimator, True)),
                f"noise={noise};estimator={estimator};margin_on minus margin_off"))
    write_json(root / "clock_analysis.json", {"groups": rows, "paired_contrasts": contrasts})
    lines = ["# Clock estimator and margin ablation", "",
             "Same holdover traffic, ten paired seeds, 16-observation OLS window. "
             "Zero-noise changes synchronization measurement noise only; timestamp noise remains 8 us. "
             "Independent random streams preserve timestamp inputs across this change.", "",
             "| Sync noise (ns) | Estimator | Margin | On-time ratio | Mean outage p99 error (ns) | Heuristic coverage |",
             "|---:|---|---|---:|---:|---:|"]
    for row in rows:
        lines.append(f"| {row['noise_ns']} | {row['estimator']} | {row['margin_enabled']} | "
                     f"{row['on_time_ratio_mean']:.6f} | {row['outage_p99_error_ns_mean']:.1f} | "
                     f"{row['heuristic_margin_coverage_mean']:.4f} |")
    lines += ["", "Coverage evaluates the unchanged heuristic allowance at outage observation times, "
              "including when subtraction is disabled. It is not a calibrated confidence bound. "
              "Quantiles are calculated within runs then averaged; they are not pooled quantiles.", "",
              "| Paired contrast | Mean on-time delta | Bootstrap 95% interval |",
              "|---|---:|---|"]
    for row in contrasts:
        lines.append(f"| {row['contrast']} | {row['mean_on_time_delta']:.6f} | {row['bootstrap_95_ci']} |")
    lines += ["", "Intervals describe ten seed pairs. This fixed congested scenario cannot separate "
              "all effects of gate delay, queueing, and estimator error; lower clock error need not "
              "produce higher on-time delivery. Raw EDF controls and their paired comparisons are "
              "in REPORT.md; per-seed contrasts are in clock_analysis.json."]
    (root / "CLOCK_ANALYSIS.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
