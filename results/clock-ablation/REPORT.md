# Benchmark report

All deadline classifications below are independently recomputed from ground-truth event times.

| Scenario | Scheduler | Runs | Mean on-time ratio | Mean run p99 latency (ns) | Misses |
|---|---:|---:|---:|---:|---:|
| holdover-noisy-two-point-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-noisy-two-point-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-noisy-two-point-no-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-noisy-two-point-no-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-noisy-window-ols-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-noisy-window-ols-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-noisy-window-ols-no-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-noisy-window-ols-no-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-zero-two-point-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-zero-two-point-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-zero-two-point-no-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-zero-two-point-no-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-zero-window-ols-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-zero-window-ols-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| holdover-zero-window-ols-no-margin | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| holdover-zero-window-ols-no-margin | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |

## Paired comparisons against EDF

Intervals are deterministic percentile bootstrap intervals over paired seed deltas; they are descriptive, not a claim of external validity.

| Scenario | Scheduler | Paired seeds | Mean on-time delta | Bootstrap 95% CI |
|---|---:|---:|---:|---:|
| holdover-noisy-two-point-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-noisy-two-point-no-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-noisy-window-ols-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-noisy-window-ols-no-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-zero-two-point-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-zero-two-point-no-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-zero-window-ols-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
| holdover-zero-window-ols-no-margin | CLOCK_AWARE_EDF | 10 | -0.005870841488 | [-0.005870841488, -0.005870841488] |
