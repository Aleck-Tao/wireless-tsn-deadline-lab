# Model definition

## Time and event order

Simulation time is signed 64-bit integer nanoseconds. At equal timestamps the internal order is synchronization observation, arrival, link completion, then service tick; insertion sequence breaks remaining ties. Both links are non-preemptive and single-server. A packet cannot enter the wireless queue before its wired completion.

For a payload of `B` bytes and link rate `R` bit/s, serialization is

```text
ceil(B * 8 * 1e9 / R) + fixed_overhead_ns.
```

Wireless service rounds this duration upward to `wireless_slot_ns`. This is airtime accounting, not a PHY or a detailed MAC.

## Observation horizon

`duration_ns` is both the event-processing horizon and the end of the evaluable deadline cohort. A candidate packet at true arrival time `a` with relative deadline `d` is generated only when `a + d <= duration_ns`. The implementation evaluates the equivalent subtraction form `a <= duration_ns - d`, after first checking `d <= duration_ns`, so the admission test cannot overflow signed nanosecond time.

A packet whose absolute deadline is exactly `duration_ns` is retained because its complete decision window is observable. A later candidate whose deadline would fall beyond the horizon is right-censored: it produces no arrival or terminal event and is absent from generated-packet and deadline-ratio denominators. The packet-budget validator uses this same last-observable-arrival calculation, including for burst multiplicity.

## Wired gate

Each cycle has a critical window followed by a background window. A packet whose priority is at least `critical_priority_min` is eligible only in the critical window; other packets are eligible only in the background window. The first eligible packet in the wired FIFO is selected.

This is a two-class, Qbv-inspired gate abstraction. It does not parse or produce gate control lists, model guard bands, implement IEEE 802.1Q, or claim compatibility.

## Wireless channel and reliability

The channel has GOOD and BAD states. Once per fixed simulation tick it draws a state transition and a separate loss sample. The four probabilities in the config are therefore **per tick**, not continuous-time rates. The draw is advanced whether or not a packet transmits, so every scheduler sees the same time-indexed channel path for a given config and seed. A transmission beginning on that tick uses that state and loss draw.

Copies share one serial wireless channel. Each copy receives at most `1 + max_retries` attempts. Eligible flows enqueue `1 + extra_replicas` copies. The first successful copy terminates the packet; queued redundant work is removed. There is no simultaneous multi-radio transmission or independence claim between copies.

Run metadata records `max_retries`, `extra_replicas`, `replication_priority_min`, and each flow's priority and derived `copies_total`. The independent validator requires copy identifiers to be a contiguous zero-based prefix and attempts for each used copy to be contiguous from one, within those bounds; every retry must follow a logged loss for the same copy. A `RETRY_AND_REPLICA_BUDGET_EXHAUSTED` drop is accepted only after every configured copy has a logged loss for every allowed attempt. `END_OF_SIMULATION` is a distinct terminal reason: the log can prove it occurred exactly at `duration_ns`, but it does not claim that retry/replica budgets were exhausted.

## Clocks and synchronization

For flow `f`, the source clock error at true time `t` is

```text
offset_f + round(t * drift_ppm_f / 1e6).
```

The packet carries a local deadline equal to its true deadline plus source-clock error and optional timestamp noise. Synchronization samples observe true error plus Gaussian measurement noise. Gaussian samples are clipped at six configured standard deviations so adversarial numeric inputs cannot overflow the integer time domain. After two accepted observations the estimator uses their slope for drift; the estimate is limited to the same ±100,000 ppm numeric safety envelope accepted for source clocks. During a configured outage it performs holdover. The bound used by clock-aware EDF is

```text
3 * measurement_noise_ns + ceil(age_ns * drift_uncertainty_ppm / 1e6).
```

Clock-aware EDF solves the estimator's linear clock equation to convert the local deadline to estimated global time, predicts uncertainty at the later of that deadline and the scheduling instant, then subtracts that uncertainty. The uncertainty term is a scheduling heuristic, not a calibrated confidence interval. This is not IEEE 802.1AS/PTP protocol behavior.

## Scheduling policies

- `FIFO`: earliest wireless enqueue sequence.
- `STRICT_PRIORITY`: highest integer priority, FIFO to break ties.
- `EDF`: smallest uncorrected local deadline, FIFO to break ties.
- `CLOCK_AWARE_EDF`: smallest offset-corrected and uncertainty-adjusted deadline, then priority and FIFO.

The scheduler does not read a packet's true global deadline. Ground-truth deadline fields are retained only for logging and independent evaluation.

## Configuration format

Configs are UTF-8 `key=value` files. Unknown keys, non-finite numbers, invalid probability ranges, duplicate flow IDs, unsafe retry/replica bounds, zero or more than one million fully observable generated packets, and more than five million scheduled tick/sync events are rejected. A flow has ten comma-separated fields:

```text
flow=id,kind,start_ns,period_ns,burst_size,payload_bytes,relative_deadline_ns,priority,clock_offset_ns,clock_drift_ppm
```

`burst_size` packets arrive at the same instant for a burst flow; a periodic flow emits one packet per period.
