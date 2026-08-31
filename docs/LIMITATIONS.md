# Limitations and claim boundary

This is a deterministic packet/slot-level research instrument, not a networking product or testbed.

- Wireless loss is a two-state stochastic abstraction. There is no waveform, modulation/coding, fading geometry, interference field, rate adaptation, contention, capture, or PHY calibration.
- The wireless link is one non-preemptive server. Replicas consume that same server and are not independent paths.
- The wired gate has two eligibility windows. It is only Qbv-inspired and omits gate control lists, guard bands, preemption, switch fabric, queue residence details, and standards interfaces.
- Synchronization observations are synthetic scalar measurements. There is no PTP/802.1AS packet exchange, residence-time correction, transparent clock, oscillator temperature model, or conformance evidence.
- Scheduler decisions occur only on fixed ticks. The results depend on tick granularity and configured per-tick channel probabilities.
- Bootstrap intervals describe the fixed synthetic seed set. They do not establish external validity or statistical power for a deployment.
- Random-engine seeds are fixed, but C++ distribution transforms may differ between standard-library implementations. The Ubuntu GCC/libstdc++ CI job is the release reference for trace-level reproduction.

The repository is not evidence of FPGA, SDR, openwifi, ns-3, hardware integration, over-the-air testing, IEEE 802.1Qbv compliance, or IEEE 802.1AS expertise. Those require separate implementations and measurements.
