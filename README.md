# Wireless TSN Deadline Lab

[![CI](https://github.com/Aleck-Tao/wireless-tsn-deadline-lab/actions/workflows/ci.yml/badge.svg)](https://github.com/Aleck-Tao/wireless-tsn-deadline-lab/actions/workflows/ci.yml)

Can correcting source-clock error improve packet deadline scheduling when wireless service is congested? This C++20 simulator compares FIFO, strict priority, EDF, and clock-aware EDF at a wired-to-wireless boundary.

Packets pass through a repeating wired gate and a serial wireless link. The model includes burst traffic, correlated losses, retries, replicas, and clock drift with synchronization outages. A Python evaluator reconstructs delivery outcomes from the event timestamps.

## Findings

The [release report](results/release/REPORT.md) contains six scenarios, four schedulers, and ten paired seeds. Values below are mean on-time delivery ratios.

| Scenario | FIFO | EDF | Clock-aware EDF | Strict priority |
|---|---:|---:|---:|---:|
| Low load | 1.000 | 1.000 | 1.000 | 1.000 |
| Bursty channel | 0.435 | 0.635 | 0.669 | 0.801 |
| Deadline contention | 0.0068 | 0.0051 | 0.0051 | 0.0866 |
| Retry + replication | 0.618 | 0.866 | 0.908 | 0.920 |
| Sync holdover | 0.060 | 0.039 | 0.033 | 0.181 |
| Overload | 0.000 | 0.000 | 0.000 | 0.000 |

- Clock-aware EDF improves on raw-clock EDF in the bursty and retry/replication cases. Its gain over FIFO is 0.234 and 0.290 respectively; the paired bootstrap intervals are in the report.
- Synchronization outages reverse that improvement. In the holdover case, clock-aware EDF reaches 0.033 against EDF's 0.039. A two-sample drift estimate can amplify measurement noise during extrapolation; [the analysis notes](docs/ANALYSIS.md) quantify that mechanism using the current parameters and a retained trace.
- Strict priority has the highest on-time ratio in several stress cases, but that does not make it best on every latency measure. In the bursty case its mean run p99 delivered latency is 7.96 ms, compared with 5.37 ms for EDF. These quantiles concern each policy's delivered packets, so the delivered populations differ.
- Every policy succeeds at low load and misses every deadline in the overload case. The scheduler changes service order; it cannot create airtime. The deadline-contention case is also severe enough that even the best on-time ratio remains below 0.09.

The useful result is the change in scheduler ordering across conditions. [Analysis notes](docs/ANALYSIS.md), [per-run metrics and traces](results/release/runs), and [paired comparisons](results/release/REPORT.md#paired-comparisons-against-fifo) provide the reasoning and measurements behind these observations.

The [clock-estimator ablation](results/clock-ablation/CLOCK_ANALYSIS.md) adds 160 paired runs isolating slope estimation, deadline margin, and synchronization noise. Its [plan](configs/clock-ablation/plan.json) retains the original holdover workload and ten seeds; the two-point baseline remains available.

## Build and run

Requires CMake 3.20+, a C++20 compiler, and Python 3.10+. Python analysis uses the standard library.

```text
cmake --preset release
cmake --build --preset release
ctest --preset release
python -m wtsn_eval run-plan --plan configs/smoke/plan.json --binary build/release/wtsn-sim --output artifacts/smoke --source-root .
python -m wtsn_eval verify artifacts/smoke --source-root . --binary build/release/wtsn-sim
```

For Visual Studio, build with `--config Release` and use `build/release/Release/wtsn-sim.exe`. Choose a fresh output directory for each run. The smoke plan runs 16 cases; [the reproduction guide](docs/REPRODUCIBILITY.md) gives the full release command and evaluator tests. CI builds with GCC and Clang and runs the C++ tests, Python checks, and address/undefined-behavior sanitizers before the full matrix.

## Inspect the evidence

Each run retains its config identity, seed, `events.csv`, `run_metadata.json`, and independently computed `metrics.json`. The evaluator checks timestamp causality, packet conservation, link overlap, retry/replica budgets, and the complete scenario/scheduler/seed matrix. Only packets whose deadlines fit inside the observation window enter the denominator.

```text
python -m wtsn_eval verify results/release --source-root .
```

This checks the committed source and result hashes. It reports `binary_verified=false` because the original CI executable is not included; fresh CI runs also verify that executable's hash. The [manifest](results/release/provenance.json) and [experiment protocol](docs/EXPERIMENTS.md) record the details.

## Model scope

The results describe a packet/slot simulation with a two-state loss channel, synthetic clock observations, and a Qbv-inspired gate. They support reasoning about this scheduling abstraction, not hardware performance or IEEE protocol conformance. [Model equations](docs/MODEL.md) and [limitations](docs/LIMITATIONS.md) describe the service, channel, and clock assumptions.

MIT licensed. Citation metadata is in [CITATION.cff](CITATION.cff).
