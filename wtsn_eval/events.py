"""Strict parser and independent invariant checker for simulator outputs."""

from __future__ import annotations

import csv
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


EVENT_FIELDS = (
    "sequence",
    "time_ns",
    "event",
    "packet_id",
    "flow_id",
    "copy_id",
    "attempt",
    "link",
    "start_ns",
    "end_ns",
    "payload_bytes",
    "queue_depth",
    "channel_state",
    "outcome",
    "deadline_ns",
    "local_deadline_ns",
    "true_clock_error_ns",
    "estimated_clock_error_ns",
    "uncertainty_ns",
    "detail",
)
INTEGER_FIELDS = frozenset(
    {
        "sequence",
        "time_ns",
        "packet_id",
        "copy_id",
        "attempt",
        "start_ns",
        "end_ns",
        "payload_bytes",
        "queue_depth",
        "deadline_ns",
        "local_deadline_ns",
        "true_clock_error_ns",
        "estimated_clock_error_ns",
        "uncertainty_ns",
    }
)

EVENT_NAMES = {
    "SYNC_OBSERVE",
    "SYNC_OUTAGE",
    "ARRIVAL",
    "WIRED_TX",
    "WIRED_RX",
    "WIRELESS_TX",
    "WIRELESS_RX",
    "DELIVER",
    "DROP",
}
SCHEDULERS = {"FIFO", "STRICT_PRIORITY", "EDF", "CLOCK_AWARE_EDF"}


@dataclass(frozen=True)
class RunData:
    events: tuple[dict[str, object], ...]
    metadata: dict[str, object]


def load_run(run_directory: Path | str) -> RunData:
    directory = Path(run_directory)
    with (directory / "run_metadata.json").open(encoding="utf-8") as handle:
        metadata = json.load(handle)
    with (directory / "events.csv").open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        actual_fields = tuple(reader.fieldnames or ())
        if actual_fields != EVENT_FIELDS:
            counts = Counter(actual_fields)
            duplicates = sorted(field for field, count in counts.items() if count > 1)
            missing = sorted(set(EVENT_FIELDS) - set(actual_fields))
            extra = sorted(set(actual_fields) - set(EVENT_FIELDS))
            raise ValueError(
                "event schema mismatch; header order must be exact; "
                f"duplicates={duplicates}, missing={missing}, extra={extra}"
            )
        rows: list[dict[str, object]] = []
        for line_number, raw in enumerate(reader, start=2):
            if None in raw or set(raw) != set(EVENT_FIELDS) or any(raw[field] is None for field in EVENT_FIELDS):
                raise ValueError(f"malformed CSV column count on line {line_number}")
            row: dict[str, object] = dict(raw)
            try:
                for field in INTEGER_FIELDS:
                    row[field] = int(str(row[field]))
            except (TypeError, ValueError) as error:
                raise ValueError(f"non-integer field on CSV line {line_number}") from error
            rows.append(row)
    data = RunData(tuple(rows), metadata)
    validate_run(data)
    return data


def validate_run(data: RunData) -> None:
    events = data.events
    metadata = data.metadata
    if metadata.get("schema_version") != "1.0":
        raise ValueError("unsupported run metadata schema")
    duration = _metadata_int(metadata, "duration_ns")
    if duration <= 0:
        raise ValueError("duration_ns must be positive")
    if not isinstance(metadata.get("scenario_id"), str) or not metadata["scenario_id"]:
        raise ValueError("scenario_id must be a non-empty string")
    if metadata.get("scheduler") not in SCHEDULERS:
        raise ValueError("metadata scheduler is unsupported")
    if _metadata_int(metadata, "seed") < 0:
        raise ValueError("seed must be non-negative")
    max_retries = _metadata_int(metadata, "max_retries")
    extra_replicas = _metadata_int(metadata, "extra_replicas")
    replication_priority_min = _metadata_int(metadata, "replication_priority_min")
    if not 0 <= max_retries <= 100 or not 0 <= extra_replicas <= 16:
        raise ValueError("retry or replica metadata exceeds supported bounds")
    copy_budgets = _flow_copy_budgets(
        metadata, extra_replicas, replication_priority_min
    )

    arrivals: dict[int, dict[str, object]] = {}
    terminals: dict[int, dict[str, object]] = {}
    last_link_end: dict[str, int] = {}
    transmissions: dict[tuple[str, int, int, int], int] = {}
    receptions: set[tuple[str, int, int, int]] = set()
    wired_received: set[int] = set()
    successful_wireless: dict[int, tuple[int, int, int]] = {}
    wireless_outcomes: dict[tuple[int, int, int], str] = {}
    wireless_attempts: dict[tuple[int, int], list[int]] = {}
    previous_time = -1

    for index, event in enumerate(events):
        sequence = _event_int(event, "sequence")
        time_ns = _event_int(event, "time_ns")
        name = str(event["event"])
        packet_id = _event_int(event, "packet_id")
        copy_id = _event_int(event, "copy_id")
        attempt = _event_int(event, "attempt")
        if sequence != index:
            raise ValueError("event sequence must be contiguous from zero")
        if time_ns < previous_time or time_ns < 0 or time_ns > duration:
            raise ValueError("event time is non-monotonic or outside the run")
        previous_time = time_ns
        if name not in EVENT_NAMES:
            raise ValueError(f"unknown event type: {name}")

        if name == "ARRIVAL":
            if packet_id == 0 or packet_id in arrivals:
                raise ValueError("packet arrival identifier is zero or duplicated")
            if (
                _event_int(event, "deadline_ns") < time_ns
                or _event_int(event, "deadline_ns") > duration
                or _event_int(event, "payload_bytes") <= 0
                or not event["flow_id"]
                or event["flow_id"] not in copy_budgets
            ):
                raise ValueError(
                    "packet arrival has invalid or right-censored deadline, payload, or flow"
                )
            arrivals[packet_id] = event
        elif packet_id != 0:
            if packet_id not in arrivals:
                raise ValueError("packet activity precedes its arrival")
            if packet_id in terminals:
                raise ValueError("packet activity occurs after terminal state")
            arrival = arrivals[packet_id]
            for field in ("flow_id", "deadline_ns", "local_deadline_ns", "payload_bytes"):
                if event[field] != arrival[field]:
                    raise ValueError(f"packet field {field} changes after arrival")
        elif name not in {"SYNC_OBSERVE", "SYNC_OUTAGE"}:
            raise ValueError("packet events require a non-zero packet identifier")

        if name in {"WIRELESS_TX", "WIRELESS_RX", "DELIVER"}:
            flow_id = str(event["flow_id"])
            copies_total = copy_budgets.get(flow_id)
            if copies_total is None or not 0 <= copy_id < copies_total:
                raise ValueError("wireless copy_id is outside the configured flow budget")
            if not 1 <= attempt <= max_retries + 1:
                raise ValueError("wireless attempt is outside the configured retry budget")
        elif copy_id != 0 or attempt != 0:
            raise ValueError("non-wireless event carries copy or attempt identity")
        if name in {"SYNC_OBSERVE", "SYNC_OUTAGE"} and event["flow_id"] not in copy_budgets:
            raise ValueError("synchronization event references an unknown flow")

        if name in {"WIRED_TX", "WIRELESS_TX"}:
            link = str(event["link"])
            start = _event_int(event, "start_ns")
            end = _event_int(event, "end_ns")
            if link not in {"wired", "wireless"} or start != time_ns or end <= start:
                raise ValueError("transmission interval is non-causal")
            if start < last_link_end.get(link, -1):
                raise ValueError(f"capacity overlap on {link}")
            last_link_end[link] = end
            key = (link, packet_id, copy_id, attempt)
            if key in transmissions:
                raise ValueError("duplicate transmission identity")
            if name == "WIRELESS_TX" and packet_id not in wired_received:
                raise ValueError("wireless transmission precedes wired reception")
            if name == "WIRELESS_TX":
                attempts = wireless_attempts.setdefault((packet_id, copy_id), [])
                if attempt != len(attempts) + 1:
                    raise ValueError("wireless attempts must be contiguous from one per copy")
                if attempt > 1 and wireless_outcomes.get((packet_id, copy_id, attempt - 1)) != "LOSS":
                    raise ValueError("wireless retry does not follow a logged loss for that copy")
                attempts.append(attempt)
            transmissions[key] = end

        if name in {"WIRED_RX", "WIRELESS_RX"}:
            link = str(event["link"])
            key = (link, packet_id, copy_id, attempt)
            if key not in transmissions or transmissions[key] != time_ns or key in receptions:
                raise ValueError("reception lacks exactly one causally matching transmission")
            receptions.add(key)
            if name == "WIRED_RX":
                if event["outcome"] != "SUCCESS" or packet_id in wired_received:
                    raise ValueError("wired reception must be one successful event")
                wired_received.add(packet_id)
            elif event["outcome"] == "SUCCESS":
                if packet_id in successful_wireless:
                    raise ValueError("packet has multiple successful wireless receptions")
                successful_wireless[packet_id] = (time_ns, copy_id, attempt)
                wireless_outcomes[(packet_id, copy_id, attempt)] = "SUCCESS"
            elif event["outcome"] != "LOSS":
                raise ValueError("wireless reception outcome must be SUCCESS or LOSS")
            else:
                wireless_outcomes[(packet_id, copy_id, attempt)] = "LOSS"

        if name in {"DELIVER", "DROP"}:
            if packet_id in terminals:
                raise ValueError("packet has multiple terminal states")
            if name == "DELIVER" and successful_wireless.get(packet_id) != (
                time_ns,
                copy_id,
                attempt,
            ):
                raise ValueError("delivery lacks a prior successful wireless reception")
            if name == "DROP" and packet_id in successful_wireless:
                raise ValueError("a successfully received packet cannot be dropped")
            if name == "DROP":
                if event["outcome"] == "RETRY_AND_REPLICA_BUDGET_EXHAUSTED":
                    copies_total = copy_budgets[str(event["flow_id"])]
                    expected_losses = {
                        (packet_id, expected_copy, expected_attempt)
                        for expected_copy in range(copies_total)
                        for expected_attempt in range(1, max_retries + 2)
                    }
                    actual_losses = {
                        key
                        for key, outcome in wireless_outcomes.items()
                        if key[0] == packet_id and outcome == "LOSS"
                    }
                    if actual_losses != expected_losses:
                        raise ValueError(
                            "retry/replica budget drop occurs before every copy is exhausted"
                        )
                elif event["outcome"] == "END_OF_SIMULATION":
                    if time_ns != duration:
                        raise ValueError("end-of-simulation drop must occur at duration_ns")
                else:
                    raise ValueError("drop outcome is unsupported")
            terminals[packet_id] = event

    if set(transmissions) != receptions:
        raise ValueError("one or more transmissions lack a matching reception")
    used_copies: dict[int, set[int]] = {}
    for packet_id, copy_id in wireless_attempts:
        used_copies.setdefault(packet_id, set()).add(copy_id)
    for packet_id, copies in used_copies.items():
        if copies != set(range(max(copies) + 1)):
            raise ValueError(f"wireless copy identifiers are not contiguous for packet {packet_id}")
    if set(arrivals) != set(terminals):
        raise ValueError("packet conservation failed: every arrival needs one terminal")
    delivered = sum(event["event"] == "DELIVER" for event in terminals.values())
    dropped = len(terminals) - delivered
    if len(arrivals) != _metadata_int(metadata, "generated_packets"):
        raise ValueError("generated count disagrees with event log")
    if delivered != _metadata_int(metadata, "delivered_packets"):
        raise ValueError("delivered count disagrees with event log")
    if dropped != _metadata_int(metadata, "dropped_packets"):
        raise ValueError("dropped count disagrees with event log")


def packet_events(events: Iterable[dict[str, object]], name: str) -> list[dict[str, object]]:
    return [event for event in events if event["event"] == name]


def _event_int(event: dict[str, object], name: str) -> int:
    value = event[name]
    if type(value) is not int:
        raise ValueError(f"event field {name} is not an integer")
    return value


def _metadata_int(metadata: dict[str, object], name: str) -> int:
    value = metadata.get(name)
    if type(value) is not int:
        raise ValueError(f"metadata field {name} is not an integer")
    return value


def _flow_copy_budgets(
    metadata: dict[str, object], extra_replicas: int, replication_priority_min: int
) -> dict[str, int]:
    flows = metadata.get("flows")
    if not isinstance(flows, list) or not flows:
        raise ValueError("metadata flows must be a non-empty list")
    budgets: dict[str, int] = {}
    for flow in flows:
        if not isinstance(flow, dict) or set(flow) != {"id", "priority", "copies_total"}:
            raise ValueError("flow metadata requires exactly id, priority, and copies_total")
        flow_id = flow["id"]
        priority = flow["priority"]
        copies_total = flow["copies_total"]
        if (
            not isinstance(flow_id, str)
            or not flow_id
            or type(priority) is not int
            or type(copies_total) is not int
            or flow_id in budgets
        ):
            raise ValueError("flow metadata identifiers and integers are invalid")
        expected_copies = 1 + (extra_replicas if priority >= replication_priority_min else 0)
        if copies_total != expected_copies:
            raise ValueError("flow copy budget disagrees with replication configuration")
        budgets[flow_id] = copies_total
    return budgets
