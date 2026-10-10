"""Metrics computed from ground-truth timestamps rather than simulator labels."""

from __future__ import annotations

import math
from collections import defaultdict
from typing import Iterable

from .events import RunData


def nearest_rank(values: Iterable[int], percentile: float) -> int | None:
    ordered = sorted(values)
    if not ordered:
        return None
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def compute_metrics(data: RunData, availability_window_ns: int = 10_000_000) -> dict[str, object]:
    arrivals = {int(e["packet_id"]): e for e in data.events if e["event"] == "ARRIVAL"}
    terminals = {int(e["packet_id"]): e for e in data.events if e["event"] in {"DELIVER", "DROP"}}
    delivered = {packet_id: e for packet_id, e in terminals.items() if e["event"] == "DELIVER"}
    on_time = {
        packet_id: event
        for packet_id, event in delivered.items()
        if int(event["time_ns"]) <= int(arrivals[packet_id]["deadline_ns"])
    }
    latencies = [int(event["time_ns"]) - int(arrivals[p]["time_ns"]) for p, event in delivered.items()]

    by_flow_latencies: dict[str, list[tuple[int, int]]] = defaultdict(list)
    delivered_bytes_by_flow: dict[str, int] = defaultdict(int)
    for arrival in arrivals.values():
        delivered_bytes_by_flow[str(arrival["flow_id"])] += 0
    for packet_id, event in delivered.items():
        arrival = arrivals[packet_id]
        flow = str(arrival["flow_id"])
        latency = int(event["time_ns"]) - int(arrival["time_ns"])
        by_flow_latencies[flow].append((int(event["time_ns"]), latency))
        delivered_bytes_by_flow[flow] += int(arrival["payload_bytes"])
    jitter: list[int] = []
    for values in by_flow_latencies.values():
        ordered = [latency for _, latency in sorted(values)]
        jitter.extend(abs(right - left) for left, right in zip(ordered, ordered[1:]))

    sync_observed_errors = [
        abs(int(event["estimated_clock_error_ns"]) - int(event["true_clock_error_ns"]))
        for event in data.events
        if event["event"] == "SYNC_OBSERVE"
    ]
    sync_outage_errors = [
        abs(int(event["estimated_clock_error_ns"]) - int(event["true_clock_error_ns"]))
        for event in data.events
        if event["event"] == "SYNC_OUTAGE"
    ]
    sync_errors = sync_observed_errors + sync_outage_errors
    outage_events = [event for event in data.events if event["event"] == "SYNC_OUTAGE"]
    outage_covered = sum(
        abs(int(event["estimated_clock_error_ns"]) - int(event["true_clock_error_ns"]))
        <= int(event["uncertainty_ns"])
        for event in outage_events
    )
    loss_bursts: list[int] = []
    current_burst = 0
    for event in data.events:
        if event["event"] != "WIRELESS_RX":
            continue
        if event["outcome"] == "LOSS":
            current_burst += 1
        elif current_burst:
            loss_bursts.append(current_burst)
            current_burst = 0
    if current_burst:
        loss_bursts.append(current_burst)

    windows: dict[int, list[bool]] = defaultdict(list)
    if availability_window_ns <= 0:
        raise ValueError("availability_window_ns must be positive")
    for packet_id, arrival in arrivals.items():
        deadline = int(arrival["deadline_ns"])
        windows[deadline // availability_window_ns].append(packet_id in on_time)
    available_windows = sum(all(outcomes) for outcomes in windows.values())

    duration = int(data.metadata["duration_ns"])
    tx_events = [event for event in data.events if event["event"] in {"WIRED_TX", "WIRELESS_TX"}]
    airtime = {
        link: sum(
            int(event["end_ns"]) - int(event["start_ns"])
            for event in tx_events
            if event["link"] == link
        )
        for link in ("wired", "wireless")
    }
    throughputs = [bytes_count * 8 * 1_000_000_000 / duration for bytes_count in delivered_bytes_by_flow.values()]
    fairness = None
    if throughputs and any(throughputs):
        fairness = (sum(throughputs) ** 2) / (len(throughputs) * sum(value * value for value in throughputs))

    generated_count = len(arrivals)
    delivered_count = len(delivered)
    on_time_count = len(on_time)
    result: dict[str, object] = {
        "schema_version": "1.0",
        "scenario_id": data.metadata["scenario_id"],
        "scheduler": data.metadata["scheduler"],
        "seed": data.metadata["seed"],
        "generated_packets": generated_count,
        "delivered_packets": delivered_count,
        "dropped_packets": generated_count - delivered_count,
        "on_time_packets": on_time_count,
        "packet_delivery_ratio": _ratio(delivered_count, generated_count),
        "on_time_delivery_ratio": _ratio(on_time_count, generated_count),
        "deadline_miss_ratio": _ratio(generated_count - on_time_count, generated_count),
        "latency_ns": _distribution(latencies),
        "jitter_ns": _distribution(jitter),
        "sync_absolute_error_ns": _distribution(sync_errors),
        "sync_observed_absolute_error_ns": _distribution(sync_observed_errors),
        "sync_outage_holdover_absolute_error_ns": _distribution(sync_outage_errors),
        "sync_outage_observations": len(sync_outage_errors),
        "sync_outage_heuristic_margin_coverage": _ratio(outage_covered, len(outage_events)),
        "wireless_loss_burst": _distribution(loss_bursts),
        "wireless_transmissions": sum(e["event"] == "WIRELESS_TX" for e in data.events),
        "wireless_retries": sum(e["event"] == "WIRELESS_TX" and int(e["attempt"]) > 1 for e in data.events),
        "replica_transmissions": sum(e["event"] == "WIRELESS_TX" and int(e["copy_id"]) > 0 for e in data.events),
        "wired_utilization": _ratio(airtime["wired"], duration),
        "wireless_utilization": _ratio(airtime["wireless"], duration),
        "delivered_goodput_bps": sum(delivered_bytes_by_flow.values()) * 8 * 1_000_000_000 / duration,
        "jain_flow_goodput_fairness": fairness,
        "service_availability": _ratio(available_windows, len(windows)),
        "availability_active_windows": len(windows),
        "availability_window_ns": availability_window_ns,
    }
    return _normalize(result)


def _ratio(numerator: int, denominator: int) -> float | None:
    return numerator / denominator if denominator else None


def _distribution(values: list[int]) -> dict[str, int | None]:
    return {
        "count": len(values),
        "p50": nearest_rank(values, 0.50),
        "p95": nearest_rank(values, 0.95),
        "p99": nearest_rank(values, 0.99),
        "max": max(values) if values else None,
    }


def _normalize(value: object) -> object:
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("non-finite metric")
        return round(value, 12)
    if isinstance(value, dict):
        return {key: _normalize(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_normalize(item) for item in value]
    return value
