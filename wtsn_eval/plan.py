"""Validation and exact run identities for benchmark plans."""

from __future__ import annotations


SCHEDULERS = {"FIFO", "STRICT_PRIORITY", "EDF", "CLOCK_AWARE_EDF"}
RunIdentity = tuple[str, str, int]


def validate_plan(plan: object) -> dict[str, object]:
    if not isinstance(plan, dict) or plan.get("schema_version") != "1.0":
        raise ValueError("plan schema_version must be 1.0")
    seeds = plan.get("seeds")
    schedulers = plan.get("schedulers")
    scenarios = plan.get("scenarios")
    if (
        not isinstance(seeds, list)
        or not seeds
        or any(type(seed) is not int or seed < 0 for seed in seeds)
    ):
        raise ValueError("plan seeds must be non-negative integers")
    if len(set(seeds)) != len(seeds):
        raise ValueError("plan seeds must be unique")
    if (
        not isinstance(schedulers, list)
        or not schedulers
        or any(not isinstance(item, str) or item not in SCHEDULERS for item in schedulers)
    ):
        raise ValueError("plan contains an unsupported scheduler")
    if len(set(schedulers)) != len(schedulers):
        raise ValueError("plan schedulers must be unique")
    if "FIFO" not in schedulers:
        raise ValueError("plan requires FIFO as the paired comparison baseline")
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError("plan scenarios must be non-empty")
    identifiers: list[str] = []
    for scenario in scenarios:
        if not isinstance(scenario, dict) or set(scenario) != {"id", "config"}:
            raise ValueError("each scenario requires exactly id and config")
        identifier = scenario["id"]
        if (
            not isinstance(identifier, str)
            or not identifier
            or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in identifier)
        ):
            raise ValueError("scenario ids must be lower-case path-safe names")
        if not isinstance(scenario["config"], str) or not scenario["config"]:
            raise ValueError("scenario config paths must be non-empty strings")
        identifiers.append(identifier)
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("scenario ids must be unique")
    return plan


def expected_run_identities(plan: object) -> set[RunIdentity]:
    validated = validate_plan(plan)
    return {
        (str(scenario["id"]), str(scheduler), int(seed))
        for scenario in validated["scenarios"]
        for scheduler in validated["schedulers"]
        for seed in validated["seeds"]
    }

