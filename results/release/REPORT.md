# Benchmark report

All deadline classifications below are independently recomputed from ground-truth event times.

| Scenario | Scheduler | Runs | Mean on-time ratio | Mean run p99 latency (ns) | Misses |
|---|---:|---:|---:|---:|---:|
| bursty-channel | CLOCK_AWARE_EDF | 10 | 0.668974358974 | 5495000.0 | 1291 |
| bursty-channel | EDF | 10 | 0.635384615384 | 5370000.0 | 1422 |
| bursty-channel | FIFO | 10 | 0.435128205128 | 6082500.0 | 2203 |
| bursty-channel | STRICT_PRIORITY | 10 | 0.801025641026 | 7960000.0 | 776 |
| deadline-contention | CLOCK_AWARE_EDF | 10 | 0.005084745763 | 81390000.0 | 5870 |
| deadline-contention | EDF | 10 | 0.005084745763 | 81350000.0 | 5870 |
| deadline-contention | FIFO | 10 | 0.006779661017 | 81840000.0 | 5860 |
| deadline-contention | STRICT_PRIORITY | 10 | 0.086610169491 | 99585000.0 | 5389 |
| low-load | CLOCK_AWARE_EDF | 10 | 1.0 | 525000.0 | 0 |
| low-load | EDF | 10 | 1.0 | 525000.0 | 0 |
| low-load | FIFO | 10 | 1.0 | 525000.0 | 0 |
| low-load | STRICT_PRIORITY | 10 | 1.0 | 525000.0 | 0 |
| overload-negative | CLOCK_AWARE_EDF | 10 | 0.0 | 127950000.0 | 9740 |
| overload-negative | EDF | 10 | 0.0 | 127950000.0 | 9740 |
| overload-negative | FIFO | 10 | 0.0 | 127200000.0 | 9740 |
| overload-negative | STRICT_PRIORITY | 10 | 0.0 | 50050000.0 | 9740 |
| retry-replication | CLOCK_AWARE_EDF | 10 | 0.907671232877 | 4315000.0 | 337 |
| retry-replication | EDF | 10 | 0.865753424658 | 4245000.0 | 490 |
| retry-replication | FIFO | 10 | 0.618082191781 | 3395000.0 | 1394 |
| retry-replication | STRICT_PRIORITY | 10 | 0.920273972603 | 4500000.0 | 291 |
| sync-holdover | CLOCK_AWARE_EDF | 10 | 0.033268101761 | 9125000.0 | 4940 |
| sync-holdover | EDF | 10 | 0.039138943249 | 9050000.0 | 4910 |
| sync-holdover | FIFO | 10 | 0.060273972603 | 7825000.0 | 4802 |
| sync-holdover | STRICT_PRIORITY | 10 | 0.180821917808 | 19250000.0 | 4186 |

## Paired comparisons against FIFO

Intervals are deterministic percentile bootstrap intervals over paired seed deltas; they are descriptive, not a claim of external validity.

| Scenario | Scheduler | Paired seeds | Mean on-time delta | Bootstrap 95% CI |
|---|---:|---:|---:|---:|
| bursty-channel | CLOCK_AWARE_EDF | 10 | 0.233846153846 | [0.187692307693, 0.273846153846] |
| bursty-channel | EDF | 10 | 0.200256410256 | [0.139487179487, 0.257179487179] |
| bursty-channel | STRICT_PRIORITY | 10 | 0.365897435897 | [0.331025641026, 0.406666666667] |
| deadline-contention | CLOCK_AWARE_EDF | 10 | -0.001694915254 | [-0.001694915254, -0.001694915254] |
| deadline-contention | EDF | 10 | -0.001694915254 | [-0.001694915254, -0.001694915254] |
| deadline-contention | STRICT_PRIORITY | 10 | 0.079830508474 | [0.077288135593, 0.082542372881] |
| low-load | CLOCK_AWARE_EDF | 10 | 0.0 | [0.0, 0.0] |
| low-load | EDF | 10 | 0.0 | [0.0, 0.0] |
| low-load | STRICT_PRIORITY | 10 | 0.0 | [0.0, 0.0] |
| overload-negative | CLOCK_AWARE_EDF | 10 | 0.0 | [0.0, 0.0] |
| overload-negative | EDF | 10 | 0.0 | [0.0, 0.0] |
| overload-negative | STRICT_PRIORITY | 10 | 0.0 | [0.0, 0.0] |
| retry-replication | CLOCK_AWARE_EDF | 10 | 0.289589041096 | [0.280821917808, 0.297808219178] |
| retry-replication | EDF | 10 | 0.247671232877 | [0.238082191781, 0.257808219178] |
| retry-replication | STRICT_PRIORITY | 10 | 0.302191780822 | [0.295616438356, 0.308493150685] |
| sync-holdover | CLOCK_AWARE_EDF | 10 | -0.027005870842 | [-0.030136986302, -0.023483365949] |
| sync-holdover | EDF | 10 | -0.021135029354 | [-0.02446183953, -0.017612524461] |
| sync-holdover | STRICT_PRIORITY | 10 | 0.120547945206 | [0.105088062623, 0.134442270059] |
