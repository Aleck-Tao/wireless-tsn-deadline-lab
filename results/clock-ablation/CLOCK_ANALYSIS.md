# Clock estimator and margin ablation

Same holdover traffic, ten paired seeds, 16-observation OLS window. Zero-noise changes synchronization measurement noise only; timestamp noise remains 8 us. Independent random streams preserve timestamp inputs across this change.

| Sync noise (ns) | Estimator | Margin | On-time ratio | Mean outage p99 error (ns) | Heuristic coverage |
|---:|---|---|---:|---:|---:|
| 0 | two_point | False | 0.033268 | 0.0 | 1.0000 |
| 0 | two_point | True | 0.033268 | 0.0 | 1.0000 |
| 0 | window_ols | False | 0.033268 | 0.0 | 1.0000 |
| 0 | window_ols | True | 0.033268 | 0.0 | 1.0000 |
| 5000 | two_point | False | 0.033268 | 656982.0 | 0.1152 |
| 5000 | two_point | True | 0.033268 | 656982.0 | 0.1152 |
| 5000 | window_ols | False | 0.033268 | 31440.9 | 0.8133 |
| 5000 | window_ols | True | 0.033268 | 31440.9 | 0.8133 |

Coverage evaluates the unchanged heuristic allowance at outage observation times, including when subtraction is disabled. It is not a calibrated confidence bound. Quantiles are calculated within runs then averaged; they are not pooled quantiles.

| Paired contrast | Mean on-time delta | Bootstrap 95% interval |
|---|---:|---|
| noise=0;margin=False;window_ols minus two_point | 0.000000 | (0.0, 0.0) |
| noise=0;margin=True;window_ols minus two_point | 0.000000 | (0.0, 0.0) |
| noise=0;estimator=two_point;margin_on minus margin_off | 0.000000 | (0.0, 0.0) |
| noise=0;estimator=window_ols;margin_on minus margin_off | 0.000000 | (0.0, 0.0) |
| noise=5000;margin=False;window_ols minus two_point | 0.000000 | (0.0, 0.0) |
| noise=5000;margin=True;window_ols minus two_point | 0.000000 | (0.0, 0.0) |
| noise=5000;estimator=two_point;margin_on minus margin_off | 0.000000 | (0.0, 0.0) |
| noise=5000;estimator=window_ols;margin_on minus margin_off | 0.000000 | (0.0, 0.0) |

Intervals describe ten seed pairs. This fixed congested scenario cannot separate all effects of gate delay, queueing, and estimator error; lower clock error need not produce higher on-time delivery. Raw EDF controls and their paired comparisons are in REPORT.md; per-seed contrasts are in clock_analysis.json.
