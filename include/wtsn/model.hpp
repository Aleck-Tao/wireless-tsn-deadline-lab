#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wtsn {

using TimeNs = std::int64_t;

enum class FlowKind { Periodic, Burst };
enum class SchedulerKind { Fifo, StrictPriority, Edf, ClockAwareEdf };

struct FlowSpec {
    std::string id;
    FlowKind kind{FlowKind::Periodic};
    TimeNs start_ns{0};
    TimeNs period_ns{1'000'000};
    std::uint32_t burst_size{1};
    std::uint32_t payload_bytes{256};
    TimeNs deadline_ns{1'000'000};
    int priority{1};
    TimeNs clock_offset_ns{0};
    double clock_drift_ppm{0.0};
};

struct SimulationConfig {
    std::string scenario_id{"unnamed"};
    TimeNs duration_ns{100'000'000};
    TimeNs tick_ns{50'000};
    std::uint64_t seed{1};
    SchedulerKind scheduler{SchedulerKind::Fifo};

    TimeNs gate_cycle_ns{1'000'000};
    TimeNs gate_critical_window_ns{500'000};
    int critical_priority_min{5};
    std::uint64_t wired_rate_bps{1'000'000'000};
    TimeNs wired_overhead_ns{500};

    std::uint64_t wireless_rate_bps{24'000'000};
    TimeNs wireless_overhead_ns{40'000};
    TimeNs wireless_slot_ns{50'000};

    double channel_p_good_to_bad{0.01};
    double channel_p_bad_to_good{0.25};
    double channel_loss_good{0.001};
    double channel_loss_bad{0.5};

    std::uint32_t max_retries{1};
    std::uint32_t extra_replicas{0};
    int replication_priority_min{8};

    TimeNs timestamp_noise_ns{0};
    TimeNs sync_interval_ns{1'000'000};
    TimeNs sync_measurement_noise_ns{0};
    TimeNs sync_outage_start_ns{-1};
    TimeNs sync_outage_end_ns{-1};
    double drift_uncertainty_ppm{20.0};
    std::string sync_estimator{"two_point"};
    std::uint32_t sync_window{16};
    bool clock_margin_enabled{true};
    bool independent_clock_noise{false};

    std::vector<FlowSpec> flows;
};

struct LogEvent {
    std::uint64_t sequence{0};
    TimeNs time_ns{0};
    std::string event;
    std::uint64_t packet_id{0};
    std::string flow_id;
    std::uint32_t copy_id{0};
    std::uint32_t attempt{0};
    std::uint32_t payload_bytes{0};
    std::string link;
    TimeNs start_ns{0};
    TimeNs end_ns{0};
    std::size_t queue_depth{0};
    std::string channel_state;
    std::string outcome;
    TimeNs deadline_ns{0};
    TimeNs local_deadline_ns{0};
    TimeNs true_clock_error_ns{0};
    TimeNs estimated_clock_error_ns{0};
    TimeNs uncertainty_ns{0};
    std::string detail;
};

struct SimulationResult {
    std::string scenario_id;
    SchedulerKind scheduler{SchedulerKind::Fifo};
    std::uint64_t seed{0};
    std::size_t generated_packets{0};
    std::size_t delivered_packets{0};
    std::size_t dropped_packets{0};
    std::vector<LogEvent> events;
};

std::string to_string(SchedulerKind scheduler);
SchedulerKind scheduler_from_string(const std::string& value);

}  // namespace wtsn
