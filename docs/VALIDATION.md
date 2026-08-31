# Validation cases

The C++ test executable uses a loss-free analytic case whose first packet must arrive after 1,024 ns wired serialization plus a wireless transmission aligned to an 11,000 ns slot duration, for a 21,000 ns terminal time. It also checks a fully closed critical gate, an all-loss bounded retry/replica budget, config parsing, and duplicate-terminal rejection.

Observation-horizon regression cases place periodic candidates on both sides of the finite-window boundary. They require a packet whose deadline equals `duration_ns` to remain in the event log, while excluding the next arrival whose deadline would exceed it. A separate high-rate but fully right-censored flow proves that packet-budget validation and actual generation use the same cohort calculation; under the former arrival-only budget it would incorrectly exceed the one-million-packet limit.

The Python fixture deliberately labels an on-time delivery as `LATE`. The evaluator must still classify it on time from ground truth. Other tests inject link overlap, a duplicate terminal, delivery without a successful wireless reception, malformed plans, and a nested-source provenance mutation.

Dedicated fixtures cover synchronization holdover and two-copy retry exhaustion. They verify combined and split synchronization-error distributions, bounded/contiguous copy and attempt identifiers, exact TX/RX/DELIVER copy-attempt identity, and rejection of a retry-budget drop before all configured losses exist. End-of-simulation drops are checked separately and are not presented as budget exhaustion.

The CSV parser requires the exact ordered header emitted by C++. Duplicate, reordered, missing, or additional header fields are rejected, as are data rows that cause `csv.DictReader` to create a `None` overflow key or a missing value.

Runtime validators additionally enforce:

- contiguous event sequence and nondecreasing timestamps;
- exactly one arrival and one terminal per packet;
- no packet activity before arrival or after terminal;
- no interval overlap on either serial link;
- exactly one time-matched reception per transmission;
- delivery only after a successful wireless reception;
- agreement between event-derived and metadata packet counts.
- exact scenario/scheduler/seed identity for every planned run;
- no missing, unexpected, duplicate, or non-canonical run in the embedded benchmark matrix.

These tests validate implementation invariants and selected analytic behavior. They do not validate the abstraction against hardware or a standards reference implementation.
