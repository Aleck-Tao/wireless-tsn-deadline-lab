from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from wtsn_eval.events import RunData, load_run, validate_run
from wtsn_eval.metrics import compute_metrics
from wtsn_eval.plan import validate_plan
from wtsn_eval.provenance import verify_manifest, write_manifest
from wtsn_eval.reporting import aggregate
from wtsn_eval.runner import _validate_run_identity
from wtsn_eval.clock_analysis import paired_contrast


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "fixtures" / "run"
SYNC_FIXTURE = ROOT / "tests" / "fixtures" / "sync-run"
REPLICA_FIXTURE = ROOT / "tests" / "fixtures" / "replica-run"


class EvaluationTests(unittest.TestCase):
    def test_clock_contrast_requires_paired_seeds(self) -> None:
        with self.assertRaisesRegex(ValueError, "matching"):
            paired_contrast({1: 0.5}, {2: 0.6}, "invalid")
        result = paired_contrast({1: 0.5, 2: 0.8}, {1: 0.6, 2: 0.6}, "known")
        self.assertAlmostEqual(result["mean_on_time_delta"], -0.05)
        self.assertAlmostEqual(result["paired_seed_deltas"][2], -0.2)

    def test_outage_coverage_measures_error_against_logged_margin(self) -> None:
        data = load_run(SYNC_FIXTURE)
        events = [dict(event) for event in data.events]
        outage = next(event for event in events if event["event"] == "SYNC_OUTAGE")
        outage["uncertainty_ns"] = 39
        self.assertEqual(compute_metrics(RunData(tuple(events), data.metadata))[
            "sync_outage_heuristic_margin_coverage"], 0.0)
        outage["uncertainty_ns"] = 40
        self.assertEqual(compute_metrics(RunData(tuple(events), data.metadata))[
            "sync_outage_heuristic_margin_coverage"], 1.0)

    def test_fixture_metrics_use_ground_truth_not_outcome_label(self) -> None:
        data = load_run(FIXTURE)
        metrics = compute_metrics(data)
        self.assertEqual(metrics["generated_packets"], 2)
        self.assertEqual(metrics["on_time_packets"], 1)
        self.assertEqual(metrics["on_time_delivery_ratio"], 0.5)
        self.assertEqual(metrics["latency_ns"]["p99"], 5000)
        self.assertEqual(metrics["wireless_retries"], 1)
        self.assertEqual(metrics["wireless_loss_burst"]["max"], 2)

    def test_capacity_overlap_is_rejected(self) -> None:
        data = load_run(FIXTURE)
        events = [dict(event) for event in data.events]
        events[4]["start_ns"] = 500
        events[4]["time_ns"] = 500
        events[4]["end_ns"] = 1500
        events[4]["sequence"] = 3
        events[3]["sequence"] = 4
        events[3], events[4] = events[4], events[3]
        with self.assertRaisesRegex(ValueError, "capacity overlap"):
            validate_run(RunData(tuple(events), data.metadata))

    def test_arrival_beyond_observation_horizon_is_rejected(self) -> None:
        data = load_run(FIXTURE)
        events = [dict(event) for event in data.events]
        events[0]["deadline_ns"] = int(data.metadata["duration_ns"]) + 1
        with self.assertRaisesRegex(ValueError, "right-censored deadline"):
            validate_run(RunData(tuple(events), data.metadata))

    def test_duplicate_terminal_is_rejected(self) -> None:
        data = load_run(FIXTURE)
        events = [dict(event) for event in data.events]
        duplicate = copy.deepcopy(events[8])
        duplicate["sequence"] = len(events)
        duplicate["time_ns"] = events[-1]["time_ns"]
        events.append(duplicate)
        with self.assertRaisesRegex(ValueError, "after terminal|multiple terminal"):
            validate_run(RunData(tuple(events), data.metadata))

    def test_delivery_requires_successful_wireless_reception(self) -> None:
        data = load_run(FIXTURE)
        events = [dict(event) for event in data.events]
        events[7]["outcome"] = "LOSS"
        with self.assertRaisesRegex(ValueError, "lacks a prior successful"):
            validate_run(RunData(tuple(events), data.metadata))

    def test_plan_rejects_duplicate_seed_and_unknown_scheduler(self) -> None:
        with self.assertRaises(ValueError):
            validate_plan(
                {
                    "schema_version": "1.0",
                    "seeds": [1, 1],
                    "schedulers": ["MAGIC"],
                    "scenarios": [{"id": "x", "config": "x.cfg"}],
                }
            )

    def test_manifest_covers_nested_source_and_exact_results(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            artifact = root / "artifact"
            nested = source / "nested" / "deeper"
            nested.mkdir(parents=True)
            nested_results = source / "module" / "results"
            nested_results.mkdir(parents=True)
            artifact.mkdir()
            (nested / "model.cpp").write_text("int model = 1;\n", encoding="utf-8")
            (nested_results / "logic.py").write_text("VALUE = 1\n", encoding="utf-8")
            (artifact / "metrics.json").write_text("{}\n", encoding="utf-8")
            binary = root / "simulator.bin"
            binary.write_bytes(b"simulator")
            write_manifest(artifact, source, binary)
            self.assertEqual(
                verify_manifest(artifact, source),
                {"source_files": 2, "result_files": 1, "binary_verified": False},
            )
            self.assertEqual(
                verify_manifest(artifact, source, binary),
                {"source_files": 2, "result_files": 1, "binary_verified": True},
            )
            wrong_binary = root / "wrong-simulator.bin"
            wrong_binary.write_bytes(b"different")
            with self.assertRaisesRegex(ValueError, "binary hash does not match"):
                verify_manifest(artifact, source, wrong_binary)
            (nested / "model.cpp").write_text("int model = 2;\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "source hashes changed"):
                verify_manifest(artifact, source)
            (nested / "model.cpp").write_text("int model = 1;\n", encoding="utf-8")
            (artifact / "unexpected.txt").write_text("unexpected\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "result file set changed"):
                verify_manifest(artifact, source)

    def test_json_outputs_are_strict(self) -> None:
        data = load_run(FIXTURE)
        serialized = json.dumps(compute_metrics(data), allow_nan=False)
        self.assertNotIn("NaN", serialized)

    def test_plans_reference_matching_configs_and_fixed_matrix_sizes(self) -> None:
        allowed_keys = {
            "scenario_id", "duration_ns", "tick_ns", "seed", "scheduler",
            "gate_cycle_ns", "gate_critical_window_ns", "critical_priority_min",
            "wired_rate_bps", "wired_overhead_ns", "wireless_rate_bps",
            "wireless_overhead_ns", "wireless_slot_ns", "channel_p_good_to_bad",
            "channel_p_bad_to_good", "channel_loss_good", "channel_loss_bad",
            "max_retries", "extra_replicas", "replication_priority_min",
            "timestamp_noise_ns", "sync_interval_ns", "sync_measurement_noise_ns",
            "sync_outage_start_ns", "sync_outage_end_ns", "drift_uncertainty_ppm", "flow",
            "sync_estimator", "sync_window", "clock_margin_enabled", "independent_clock_noise",
        }
        expected_runs = {"smoke": 16, "release": 240, "clock-ablation": 160}
        for plan_kind, run_count in expected_runs.items():
            plan_path = ROOT / "configs" / plan_kind / "plan.json"
            plan = json.loads(plan_path.read_text(encoding="utf-8"))
            validate_plan(plan)
            self.assertEqual(
                len(plan["seeds"]) * len(plan["schedulers"]) * len(plan["scenarios"]),
                run_count,
            )
            for scenario in plan["scenarios"]:
                config_path = ROOT / scenario["config"]
                self.assertTrue(config_path.is_file())
                pairs = []
                for raw_line in config_path.read_text(encoding="utf-8").splitlines():
                    line = raw_line.strip()
                    if not line or line.startswith("#"):
                        continue
                    self.assertIn("=", line)
                    key, value = (part.strip() for part in line.split("=", 1))
                    self.assertIn(key, allowed_keys)
                    self.assertTrue(value)
                    if key == "flow":
                        self.assertEqual(len([part.strip() for part in value.split(",")]), 10)
                    pairs.append((key, value))
                identifiers = [value for key, value in pairs if key == "scenario_id"]
                self.assertEqual(identifiers, [scenario["id"]])

    def test_aggregate_uses_paired_seed_deltas(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary)
            plan = {
                "schema_version": "1.0",
                "scenarios": [{"id": "case", "config": "unused.cfg"}],
                "schedulers": ["FIFO", "EDF"],
                "seeds": [11, 23],
            }
            (artifact / "benchmark_plan.json").write_text(
                json.dumps(plan), encoding="utf-8"
            )
            for scheduler, ratios in {"FIFO": [0.5, 0.6], "EDF": [0.7, 0.9]}.items():
                for seed, ratio in zip((11, 23), ratios):
                    leaf = artifact / "runs" / "case" / scheduler / f"seed-{seed}"
                    leaf.mkdir(parents=True)
                    payload = {
                        "scenario_id": "case",
                        "scheduler": scheduler,
                        "seed": seed,
                        "on_time_delivery_ratio": ratio,
                        "latency_ns": {"p99": 1000},
                        "generated_packets": 10,
                        "on_time_packets": int(ratio * 10),
                    }
                    (leaf / "metrics.json").write_text(json.dumps(payload), encoding="utf-8")
            summary, comparisons = aggregate(artifact)
            self.assertEqual(len(summary), 2)
            comparison = comparisons["comparisons"][0]
            self.assertEqual(comparison["paired_seeds"], [11, 23])
            self.assertAlmostEqual(comparison["on_time_ratio_delta_mean"], 0.25)
            self.assertTrue((artifact / "REPORT.md").is_file())

    def test_run_identity_requires_exact_scenario_scheduler_and_seed(self) -> None:
        metadata = {"scenario_id": "case", "scheduler": "EDF", "seed": 11}
        _validate_run_identity(metadata, "case", "EDF", 11)
        for changed in (
            {**metadata, "scenario_id": "other"},
            {**metadata, "scheduler": "FIFO"},
            {**metadata, "seed": 12},
        ):
            with self.assertRaisesRegex(ValueError, "run identity mismatch"):
                _validate_run_identity(changed, "case", "EDF", 11)

    def test_aggregate_rejects_missing_and_duplicate_matrix_runs(self) -> None:
        plan = {
            "schema_version": "1.0",
            "scenarios": [{"id": "case", "config": "unused.cfg"}],
            "schedulers": ["FIFO"],
            "seeds": [11, 23],
        }
        base_metric = {
            "scenario_id": "case",
            "scheduler": "FIFO",
            "seed": 11,
            "on_time_delivery_ratio": 1.0,
            "latency_ns": {"p99": 1000},
            "generated_packets": 1,
            "on_time_packets": 1,
        }
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary)
            (artifact / "benchmark_plan.json").write_text(json.dumps(plan), encoding="utf-8")
            leaf = artifact / "runs" / "case" / "FIFO" / "seed-11"
            leaf.mkdir(parents=True)
            (leaf / "metrics.json").write_text(json.dumps(base_metric), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "benchmark matrix mismatch"):
                aggregate(artifact)

        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary)
            duplicate_plan = {**plan, "seeds": [11]}
            (artifact / "benchmark_plan.json").write_text(
                json.dumps(duplicate_plan), encoding="utf-8"
            )
            canonical = artifact / "runs" / "case" / "FIFO" / "seed-11"
            duplicate = artifact / "runs" / "duplicate"
            canonical.mkdir(parents=True)
            duplicate.mkdir(parents=True)
            encoded = json.dumps(base_metric)
            (canonical / "metrics.json").write_text(encoded, encoding="utf-8")
            (duplicate / "metrics.json").write_text(encoded, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate run identity"):
                aggregate(artifact)

    def test_sync_metrics_include_and_separate_outage_holdover_error(self) -> None:
        metrics = compute_metrics(load_run(SYNC_FIXTURE))
        self.assertEqual(metrics["sync_absolute_error_ns"]["count"], 3)
        self.assertEqual(metrics["sync_absolute_error_ns"]["max"], 40)
        self.assertEqual(metrics["sync_observed_absolute_error_ns"]["count"], 2)
        self.assertEqual(metrics["sync_observed_absolute_error_ns"]["max"], 10)
        self.assertEqual(metrics["sync_outage_holdover_absolute_error_ns"]["count"], 1)
        self.assertEqual(metrics["sync_outage_holdover_absolute_error_ns"]["max"], 40)
        self.assertEqual(metrics["sync_outage_observations"], 1)

    def test_replica_fixture_proves_complete_loss_budget(self) -> None:
        data = load_run(REPLICA_FIXTURE)
        self.assertEqual(data.metadata["extra_replicas"], 1)
        self.assertEqual(
            sum(event["event"] == "WIRELESS_TX" for event in data.events), 4
        )

    def test_premature_replica_budget_drop_is_rejected(self) -> None:
        data = load_run(REPLICA_FIXTURE)
        metadata = copy.deepcopy(data.metadata)
        metadata["max_retries"] = 2
        with self.assertRaisesRegex(ValueError, "before every copy is exhausted"):
            validate_run(RunData(data.events, metadata))

    def test_wireless_attempt_gap_and_delivery_identity_are_rejected(self) -> None:
        replica = load_run(REPLICA_FIXTURE)
        events = [dict(event) for event in replica.events]
        events[4]["attempt"] = 2
        with self.assertRaisesRegex(ValueError, "causally matching transmission"):
            validate_run(RunData(tuple(events), replica.metadata))

        events = [dict(event) for event in replica.events]
        events[7]["attempt"] = 3
        events[8]["attempt"] = 3
        metadata = copy.deepcopy(replica.metadata)
        metadata["max_retries"] = 2
        with self.assertRaisesRegex(ValueError, "contiguous from one"):
            validate_run(RunData(tuple(events), metadata))

        events = [dict(event) for event in replica.events]
        events[4]["outcome"] = "SUCCESS"
        with self.assertRaisesRegex(ValueError, "retry does not follow a logged loss"):
            validate_run(RunData(tuple(events), replica.metadata))

        data = load_run(FIXTURE)
        events = [dict(event) for event in data.events]
        events[8]["attempt"] = 2
        with self.assertRaisesRegex(ValueError, "successful wireless reception"):
            validate_run(RunData(tuple(events), data.metadata))

    def test_wireless_copy_range_and_continuity_are_rejected(self) -> None:
        replica = load_run(REPLICA_FIXTURE)
        events = [dict(event) for event in replica.events]
        events[5]["copy_id"] = 2
        with self.assertRaisesRegex(ValueError, "outside the configured flow budget"):
            validate_run(RunData(tuple(events), replica.metadata))

        events = [dict(event) for event in replica.events]
        for index in (5, 6, 9, 10):
            events[index]["copy_id"] = 2
        events[10]["outcome"] = "SUCCESS"
        events[11]["event"] = "DELIVER"
        events[11]["copy_id"] = 2
        events[11]["attempt"] = 2
        events[11]["outcome"] = "ON_TIME"
        metadata = copy.deepcopy(replica.metadata)
        metadata["extra_replicas"] = 2
        metadata["flows"][0]["copies_total"] = 3
        metadata["delivered_packets"] = 1
        metadata["dropped_packets"] = 0
        with self.assertRaisesRegex(ValueError, "copy identifiers are not contiguous"):
            validate_run(RunData(tuple(events), metadata))

    def test_csv_header_and_row_width_are_strict(self) -> None:
        lines = (FIXTURE / "events.csv").read_text(encoding="utf-8").splitlines()
        header = lines[0].split(",")
        variants = {
            "duplicate": ",".join(header + ["detail"]),
            "missing": ",".join(header[:-1]),
            "extra": ",".join(header + ["unexpected"]),
            "reordered": ",".join([header[1], header[0], *header[2:]]),
        }
        for label, changed_header in variants.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                run = Path(temporary)
                (run / "events.csv").write_text(
                    "\n".join([changed_header, *lines[1:]]) + "\n", encoding="utf-8"
                )
                (run / "run_metadata.json").write_text(
                    (FIXTURE / "run_metadata.json").read_text(encoding="utf-8"),
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(ValueError, "event schema mismatch"):
                    load_run(run)

        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary)
            malformed = [lines[0], lines[1] + ",unexpected", *lines[2:]]
            (run / "events.csv").write_text("\n".join(malformed) + "\n", encoding="utf-8")
            (run / "run_metadata.json").write_text(
                (FIXTURE / "run_metadata.json").read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "malformed CSV column count"):
                load_run(run)


if __name__ == "__main__":
    unittest.main()
