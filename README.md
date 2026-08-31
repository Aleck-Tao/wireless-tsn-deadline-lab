# Wireless TSN Deadline Lab

[![CI](https://github.com/Aleck-Tao/wireless-tsn-deadline-lab/actions/workflows/ci.yml/badge.svg)](https://github.com/Aleck-Tao/wireless-tsn-deadline-lab/actions/workflows/ci.yml)

A small, inspectable C++20 discrete-event simulator for one research question: how do wireless loss bursts and imperfect clock synchronization change deadline-aware scheduling decisions at a wired-to-wireless boundary?

The repository is deliberately narrower than a network stack. It models packet/slot-level timing on ordinary CPUs and compares FIFO, strict priority, EDF, and clock-aware EDF under paired random seeds. The wired gate is **inspired by** IEEE 802.1Qbv terminology but is not an implementation, conformance test, or standards claim.

## What is implemented

- Integer-nanosecond event time and deterministic tie-breaking.
- Periodic and burst sources with payload, priority, deadline, and local-clock parameters.
- A finite observation cohort: a packet is generated only when `arrival_ns + relative_deadline_ns <= duration_ns`; equality is included, while later-deadline arrivals are right-censored rather than counted as drops.
- One serial wired link with a repeating critical/background gate and serialization delay.
- One serial wireless link with rate-derived airtime and slot alignment.
- A two-state Gilbert–Elliott loss process sampled on fixed ticks.
- Clock offset and drift, timestamp noise, synchronization measurement noise and outages, plus a two-observation drift estimator with holdover uncertainty.
- FIFO, strict priority, local-clock EDF, and uncertainty-adjusted clock-aware EDF.
- Bounded retry and optional high-priority replication.
- Append-only-within-run, write-once CSV event records, independent Python validation, normalized metrics, paired-seed comparisons, and exact SHA-256 provenance. The simulator refuses to overwrite an existing run log.

## Build and test

```text
cmake --preset release
cmake --build --preset release
ctest --preset release
python -m unittest discover -s tests/python -v
```

Run the 16-case smoke matrix after building:

```text
python -m wtsn_eval run-plan --plan configs/smoke/plan.json --binary build/release/wtsn-sim --output artifacts/smoke --source-root .
python -m wtsn_eval verify artifacts/smoke --source-root . --binary build/release/wtsn-sim
```

Paths are handled through `pathlib` and `std::filesystem`; no shell-specific runner is required. GitHub Actions is the normative clean-room build: GCC and Clang compile with warnings as errors, CTest runs the C++ cases, and a separate GCC job runs AddressSanitizer plus UndefinedBehaviorSanitizer.

## Evidence chain

```text
config + seed + scheduler
        -> C++ event simulation
        -> events.csv + run_metadata.json
        -> independent invariant checks and metrics.json
        -> paired summaries
        -> provenance.json (exact source/result file sets and hashes)
```

The evaluator does not trust the simulator's `DELIVER` outcome label. It recomputes deadline status from arrival, terminal, and ground-truth deadline timestamps. Its denominator contains only packets whose full relative-deadline window fits inside the configured run; this avoids treating unobservable post-horizon outcomes as failures. It also checks the exact CSV schema, run identity, complete experiment matrix, sequence monotonicity, transmission/reception matching, link non-overlap, retry/replica bounds, packet conservation, terminal uniqueness, and causal delivery after the same successful copy and attempt.

The release plan contains six scenarios, four schedulers, and ten shared seeds (240 runs). CI uploads the resulting normalized artifact; checked-in findings should only be added after that artifact passes provenance verification. See [the experiment protocol](docs/EXPERIMENTS.md), [model definition](docs/MODEL.md), and [reproduction guide](docs/REPRODUCIBILITY.md).

## CI-verified release findings

The first 240-run matrix passed GCC and Clang release builds, CTest, 18 Python evaluator tests, ASan/UBSan, the 16-run smoke matrix, exact source/result hashing, and simulator-binary hash verification in [GitHub Actions run 33414022492](https://github.com/Aleck-Tao/wireless-tsn-deadline-lab/actions/runs/33414022492). The complete per-run evidence is retained in the [committed release report](results/release/REPORT.md).

| Scenario | FIFO | EDF | Clock-aware EDF | Strict priority |
|---|---:|---:|---:|---:|
| Low load | 1.000 | 1.000 | 1.000 | 1.000 |
| Bursty channel | 0.435 | 0.635 | 0.669 | 0.801 |
| Deadline contention | 0.0068 | 0.0051 | 0.0051 | 0.0866 |
| Retry + replication | 0.618 | 0.866 | 0.908 | 0.920 |
| Sync holdover | 0.060 | 0.039 | 0.033 | 0.181 |
| Overload negative | 0.000 | 0.000 | 0.000 | 0.000 |

Values are mean on-time delivery ratios over ten paired seeds. In this configured model, clock-aware EDF improved on FIFO under bursty loss and retry/replication, but it was worse under the synchronization-outage holdover case. Strict priority was strongest in several stress cases. These retained negative results show that the implemented uncertainty adjustment is a heuristic to investigate, not a generally superior or certified scheduler. Bootstrap intervals and all per-seed values remain in the release artifact; the findings do not generalize beyond the declared simulator, configurations, and seed set.

## Boundaries

This project contains no ns-3 model, PHY waveform, MAC implementation, FPGA/SDR bitstream, openwifi integration, hardware measurement, IEEE 802.1AS implementation, or IEEE 802.1Qbv conformance evidence. Results are synthetic evidence about the declared abstraction only. See [LIMITATIONS.md](docs/LIMITATIONS.md) before interpreting comparisons.

## Repository map

```text
include/wtsn, src/        C++20 model and CLI
configs/smoke             short CI experiment matrix
configs/release           preregistered 240-run matrix
tests/cpp                 analytic and controlled-negative model tests
tests/python              evaluator, invariant, and provenance tests
wtsn_eval                 standard-library-only analysis pipeline
docs                      model, protocol, limitations, reproduction notes
results                   only verified CI outputs belong here
```

Licensed under MIT. If reusing the software, cite the repository using `CITATION.cff`; simulation results still require the exact provenance manifest.
