"""Command-line entry points."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .events import load_run
from .metrics import compute_metrics
from .provenance import verify_manifest, write_json
from .reporting import aggregate
from .runner import run_plan


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="wtsn-eval")
    commands = parser.add_subparsers(dest="command", required=True)
    analyze = commands.add_parser("analyze", help="validate and analyze one run")
    analyze.add_argument("run_directory", type=Path)
    analyze.add_argument("--output", type=Path)
    summarize = commands.add_parser("aggregate", help="rebuild benchmark summaries")
    summarize.add_argument("artifact_root", type=Path)
    execute = commands.add_parser("run-plan", help="execute a benchmark plan")
    execute.add_argument("--plan", required=True, type=Path)
    execute.add_argument("--binary", required=True, type=Path)
    execute.add_argument("--output", required=True, type=Path)
    execute.add_argument("--source-root", required=True, type=Path)
    verify = commands.add_parser("verify", help="verify exact source and result manifests")
    verify.add_argument("artifact_root", type=Path)
    verify.add_argument("--source-root", required=True, type=Path)
    verify.add_argument("--binary", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        arguments = build_parser().parse_args(argv)
        if arguments.command == "analyze":
            metrics = compute_metrics(load_run(arguments.run_directory))
            output = arguments.output or arguments.run_directory / "metrics.json"
            write_json(output, metrics)
            print(f"validated and wrote {output}")
        elif arguments.command == "aggregate":
            summary, comparisons = aggregate(arguments.artifact_root)
            print(f"wrote {len(summary)} groups and {len(comparisons['comparisons'])} comparisons")
        elif arguments.command == "run-plan":
            count = run_plan(arguments.plan, arguments.binary, arguments.output, arguments.source_root)
            print(f"completed {count} validated simulation runs")
        elif arguments.command == "verify":
            counts = verify_manifest(
                arguments.artifact_root, arguments.source_root, arguments.binary
            )
            print(
                f"verified {counts['source_files']} source files and "
                f"{counts['result_files']} result files; "
                f"binary_verified={str(counts['binary_verified']).lower()}"
            )
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
