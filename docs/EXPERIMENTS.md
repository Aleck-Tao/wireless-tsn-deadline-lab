# Experiment protocol

## Question

When multiple source clocks disagree and wireless service is congested, does correction for estimated clock error reduce deadline misses relative to FIFO, strict priority, or EDF on raw local timestamps? How does that trade against loss bursts, retry work, and estimator holdover?

## Fixed comparison design

The release plan fixes six scenarios, all four policies, and ten seeds before results are inspected. Within a scenario, schedulers share the same seeds. Channel and clock-noise generators use separately derived streams; channel state advances on every fixed tick, independent of queue decisions. This supports paired software-simulation comparisons, not generalization to a physical deployment.

| Scenario | Intended stressor | Control/interpretation |
|---|---|---|
| `low-load` | Light service demand | Scheduling policies should be similar; large gaps merit investigation. |
| `deadline-contention` | Backlog and clock-biased deadlines | Primary scheduler comparison. |
| `bursty-channel` | Long BAD-state residence and retries | Tests reliability/latency interaction. |
| `sync-holdover` | Long observation outage and high drift | Tests estimator uncertainty behavior. |
| `retry-replication` | One extra safety-flow copy | Measures deadline benefit against added airtime. |
| `overload-negative` | Offered load beyond service budget | Controlled negative; all policies should incur misses. |

The smoke plan uses two shortened cases and two seeds to exercise the complete pipeline in pull requests. It is not used for findings.

Each run uses a fixed finite observation horizon. The analysis cohort contains exactly the scheduled packets satisfying `arrival_ns + relative_deadline_ns <= duration_ns`. Equality is retained. Candidates with deadlines after the horizon are right-censored before arrival generation, so they cannot be mislabeled as end-of-simulation drops or inflate a scheduler's miss ratio. The same cohort rule is applied to every scheduler and seed.

## Primary and diagnostic metrics

Primary: on-time delivery ratio over all fully observable generated packets. Within that cohort, a drop is a deadline miss. Deadline status is recomputed from ground-truth event fields rather than the C++ outcome string.

Diagnostics: packet delivery ratio; deadline miss ratio; nearest-rank p50/p95/p99/max delivered latency; per-flow consecutive-latency jitter; active-window service availability; delivered goodput; Jain fairness across delivered per-flow goodput; wired/wireless utilization; retry and replica transmissions; consecutive wireless-loss burst length; synchronization absolute-error quantiles and outage observation count.

`sync_absolute_error_ns` combines every scheduled synchronization evaluation epoch, including `SYNC_OUTAGE` holdover epochs for which both true and estimated clock error remain logged. `sync_observed_absolute_error_ns` contains only accepted `SYNC_OBSERVE` epochs, while `sync_outage_holdover_absolute_error_ns` isolates outage holdover. These are discrete observation-epoch distributions, not time-weighted continuous-clock error.

An active availability window is a 10 ms true-time deadline bucket containing at least one fully observable packet. The window is available only if every packet assigned to it is delivered by its deadline. Empty windows and right-censored candidates are excluded.

## Statistics

For each scenario and non-FIFO scheduler, the report computes per-seed on-time-ratio differences against FIFO and their mean. A deterministic 2,000-resample paired percentile bootstrap gives a descriptive 95% interval. With only ten seeds and synthetic scenarios, the interval is not treated as a population-level inferential claim. Full per-run data remain in the artifact; aggregation does not replace them.

## Interpretation gates

A result is reportable only if CTest, Python tests, sanitizer CTest, run validation, and exact provenance verification pass. Any scheduler improvement must be discussed alongside utilization, retry/replica work, and the overload negative. A claim about synchronization must include estimator error and outage behavior rather than deadline ratio alone.
