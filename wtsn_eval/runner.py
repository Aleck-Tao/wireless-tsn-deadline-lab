"""Execute a normalized matrix of scenarios, schedulers, and seeds."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

from .events import load_run
from .metrics import compute_metrics
from .plan import validate_plan
from .provenance import write_json, write_manifest
from .reporting import aggregate


def run_plan(
    plan_path: Path | str,
    binary_path: Path | str,
    output_path: Path | str,
    source_root: Path | str,
) -> int:
    plan_file = Path(plan_path).resolve()
    binary = Path(binary_path).resolve()
    output = Path(output_path).resolve()
    source = Path(source_root).resolve()
    with plan_file.open(encoding="utf-8") as handle:
        plan = json.load(handle)
    validate_plan(plan)
    if not source.is_dir():
        raise ValueError(f"source root does not exist: {source}")
    if not binary.is_file():
        raise ValueError(f"simulator binary does not exist: {binary}")
    try:
        relative_output = output.relative_to(source)
    except ValueError:
        relative_output = None
    if relative_output is not None and (
        not relative_output.parts or relative_output.parts[0] not in {"artifacts", "results"}
    ):
        raise ValueError("output inside source root must be under artifacts/ or results/")
    if output.exists() and not output.is_dir():
        raise ValueError(f"output path is not a directory: {output}")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory must be absent or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "benchmark_plan.json", plan)

    run_count = 0
    for scenario in plan["scenarios"]:
        config = source / str(scenario["config"])
        try:
            config.resolve().relative_to(source)
        except ValueError as error:
            raise ValueError(f"scenario config escapes source root: {config}") from error
        if not config.is_file():
            raise ValueError(f"scenario config does not exist: {config}")
        scenario_id = str(scenario["id"])
        for scheduler in plan["schedulers"]:
            for seed in plan["seeds"]:
                run_directory = output / "runs" / scenario_id / scheduler / f"seed-{seed}"
                command = [
                    str(binary),
                    "--config",
                    str(config),
                    "--output",
                    str(run_directory),
                    "--scheduler",
                    scheduler,
                    "--seed",
                    str(seed),
                ]
                completed = subprocess.run(command, check=False, capture_output=True, text=True)
                if completed.returncode != 0:
                    raise RuntimeError(
                        f"simulation failed for {scenario_id}/{scheduler}/{seed}: "
                        f"{completed.stderr.strip()}"
                    )
                data = load_run(run_directory)
                _validate_run_identity(data.metadata, scenario_id, scheduler, seed)
                write_json(run_directory / "metrics.json", compute_metrics(data))
                run_count += 1
    aggregate(output)
    write_manifest(output, source, binary)
    return run_count


def _validate_run_identity(
    metadata: dict[str, object],
    expected_scenario: str,
    expected_scheduler: str,
    expected_seed: int,
) -> None:
    expected = (expected_scenario, expected_scheduler, expected_seed)
    actual = (metadata.get("scenario_id"), metadata.get("scheduler"), metadata.get("seed"))
    if actual != expected:
        raise ValueError(f"run identity mismatch; expected={expected}, actual={actual}")


# Backward-compatible private alias used by existing tests and downstream notebooks.
_validate_plan = validate_plan
