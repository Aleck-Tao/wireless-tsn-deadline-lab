# Why the scheduler ordering changes

The [release report](../results/release/REPORT.md) shows that clock correction helps in some scenarios and hurts in holdover. The following calculations use the current implementation and committed measurements. They explain a mechanism worth testing; they are not a new parameter sweep or a proof of causation.

## Holdover amplifies a noisy slope

The estimator in [simulator.cpp](../src/simulator.cpp) takes the difference between its two latest accepted offset measurements. Write a measurement as `y_k = offset(t_k) + e_k`, with interval `Delta`. Ignoring rounding and clipping, the drift error is

```text
drift_error = (e_k - e_(k-1)) / Delta.
```

For independent, zero-mean measurement errors with standard deviation `sigma`, its standard deviation is `sqrt(2) * sigma / Delta`. The implementation samples Gaussian noise and clips it at six standard deviations; this approximation ignores that negligible tail truncation and the estimator's numeric clamp.

The [holdover configuration](../configs/release/sync-holdover.cfg) uses `sigma = 5 microseconds` and `Delta = 1 millisecond`. The resulting slope-error scale is about 7,071 ppm, much larger than the two short-deadline flows' actual drifts of +45 and −40 ppm. When extrapolating `h` past the last observation, the offset-estimation noise is

```text
(1 + h/Delta) * e_k - (h/Delta) * e_(k-1).
```

At `h = 70 ms`, its approximate standard deviation is 498.5 microseconds. Meanwhile, the scheduler's configured uncertainty allowance is only

```text
3 * 5 microseconds + 70 ms * 60 ppm = 19.2 microseconds.
```

The formula accounts for configured drift uncertainty but not this measurement-induced slope uncertainty. Increasing the margin alone would therefore need calibration; the current margin is a heuristic, not a confidence bound.

The retained [seed 11 clock-aware trace metrics](../results/release/runs/sync-holdover/CLOCK_AWARE_EDF/seed-11/metrics.json) show outage-only p99 absolute clock error of 640.703 microseconds, compared with 12.718 microseconds at accepted observations. The two short-deadline flows have 550-microsecond relative deadlines. This single trace demonstrates the scale of the holdover error. Queueing and gate delays also contribute to misses, so it does not isolate the estimator's causal contribution to the full result.

## A conservative deadline can still reorder work badly

Clock-aware EDF sorts by `estimated_global_deadline - uncertainty`, while ordinary EDF sorts by the raw local deadline. Subtracting a larger uncertainty makes a packet more urgent. A common uncertainty subtraction leaves an ordering unchanged; different clock estimates and prediction horizons can change it.

That adjustment protects against one kind of late service but may move work ahead of another packet with less actual slack. It neither checks feasibility nor adds wireless capacity. In the present matrix, the mean on-time ratio improves from 0.635 to 0.669 in the bursty case and from 0.866 to 0.908 with retry/replication, but falls from 0.039 to 0.033 in holdover.

The [clock ablation](../results/clock-ablation/CLOCK_ANALYSIS.md) tests this explanation separately from the original release matrix. It crosses two-point versus 16-observation least-squares estimation, margin on/off, and synchronization noise of 0/5 microseconds, with raw EDF controls and the same ten seeds. Independent measurement and source-timestamp random streams prevent a noise change from also changing packet timestamps. The [paired contrast data](../results/clock-ablation/clock_analysis.json) retain positive and negative deltas. Reduced clock error is not assumed to improve deadline performance under the fixed congested traffic.

## Delivery probability competes with service time

Each copy consumes the same serial wireless link. Retries and replicas add opportunities to succeed, but also occupy airtime and delay other packets. The [model](MODEL.md#wireless-channel-and-reliability) removes queued redundant work after the first successful copy; it does not create independent radio paths.

In the retry/replication scenario, clock-aware EDF has a mean on-time ratio of 0.908 versus FIFO's 0.618, while mean run p99 delivered latency is 4.315 ms versus 3.395 ms. The higher on-time ratio and longer delivered-latency tail can coexist because policies choose which packets to serve and the delivered cohorts differ.

This scenario compares schedulers with the same replication configuration. It does not estimate the effect of enabling replication: that would require a matched replication-off run. Likewise, the all-zero overload row is evidence about the configured offered load and deadlines, not a general failure of deadline scheduling.
