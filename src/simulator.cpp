#include "wtsn/simulator.hpp"

#include "wtsn/config.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wtsn {
namespace {

enum class InternalEventType { Sync, Arrival, WiredComplete, WirelessComplete, Tick };

struct TxJob {
    std::uint64_t packet_id{0};
    std::uint32_t copy_id{0};
    std::uint32_t attempts{0};
    std::uint64_t enqueue_order{0};
};

struct InternalEvent {
    TimeNs time_ns{0};
    std::uint64_t sequence{0};
    InternalEventType type{InternalEventType::Tick};
    std::size_t flow_index{0};
    std::uint64_t packet_id{0};
    TxJob job;
    bool wireless_success{false};
    std::string channel_state;
};

struct EarlierEvent {
    static int rank(InternalEventType type) {
        switch (type) {
            case InternalEventType::Sync:
                return 0;
            case InternalEventType::Arrival:
                return 1;
            case InternalEventType::WiredComplete:
            case InternalEventType::WirelessComplete:
                return 2;
            case InternalEventType::Tick:
                return 3;
        }
        return 4;
    }

    bool operator()(const InternalEvent& left, const InternalEvent& right) const {
        if (left.time_ns != right.time_ns) {
            return left.time_ns > right.time_ns;
        }
        if (rank(left.type) != rank(right.type)) {
            return rank(left.type) > rank(right.type);
        }
        return left.sequence > right.sequence;
    }
};

struct PacketState {
    std::uint64_t id{0};
    std::size_t flow_index{0};
    TimeNs arrival_ns{0};
    TimeNs deadline_ns{0};
    TimeNs local_deadline_ns{0};
    int priority{0};
    std::uint32_t copies_total{1};
    std::uint32_t exhausted_copies{0};
    bool terminal{false};
};

struct SyncEstimator {
    bool valid{false};
    bool previous_valid{false};
    TimeNs previous_time_ns{0};
    TimeNs previous_offset_ns{0};
    TimeNs last_time_ns{0};
    TimeNs last_offset_ns{0};
    double estimated_drift_ppm{0.0};
    std::deque<std::pair<TimeNs, TimeNs>> observations;

    void update(TimeNs time_ns, TimeNs measured_offset_ns, const SimulationConfig& config) {
        if (valid) {
            previous_valid = true;
            previous_time_ns = last_time_ns;
            previous_offset_ns = last_offset_ns;
        }
        last_time_ns = time_ns;
        last_offset_ns = measured_offset_ns;
        valid = true;
        if (previous_valid && last_time_ns > previous_time_ns) {
            const auto delta_offset = static_cast<long double>(last_offset_ns - previous_offset_ns);
            const auto delta_time = static_cast<long double>(last_time_ns - previous_time_ns);
            estimated_drift_ppm = std::clamp(
                static_cast<double>(delta_offset * 1'000'000.0L / delta_time),
                -100'000.0, 100'000.0);
        }
        if (config.sync_estimator == "window_ols") {
            observations.emplace_back(time_ns, measured_offset_ns);
            while (observations.size() > config.sync_window) {
                observations.pop_front();
            }
            if (observations.size() >= 2) {
                // Center times at the latest observation to avoid large absolute-time products.
                long double mean_time = 0.0L;
                long double mean_offset = 0.0L;
                for (const auto& [time, offset] : observations) {
                    mean_time += static_cast<long double>(time - time_ns);
                    mean_offset += static_cast<long double>(offset);
                }
                const auto count = static_cast<long double>(observations.size());
                mean_time /= count;
                mean_offset /= count;
                long double covariance = 0.0L;
                long double variance = 0.0L;
                for (const auto& [time, offset] : observations) {
                    const auto centered = static_cast<long double>(time - time_ns) - mean_time;
                    covariance += centered * (static_cast<long double>(offset) - mean_offset);
                    variance += centered * centered;
                }
                const auto slope = std::clamp(covariance / variance, -0.1L, 0.1L);
                estimated_drift_ppm = static_cast<double>(slope * 1'000'000.0L);
                last_offset_ns = static_cast<TimeNs>(std::llround(mean_offset - slope * mean_time));
            }
        }
    }

    TimeNs estimate_offset(TimeNs time_ns) const {
        if (!valid) {
            return 0;
        }
        const auto elapsed = static_cast<long double>(time_ns - last_time_ns);
        const auto drift = elapsed * static_cast<long double>(estimated_drift_ppm) / 1'000'000.0L;
        return last_offset_ns + static_cast<TimeNs>(std::llround(drift));
    }

    TimeNs estimate_global_deadline(TimeNs local_deadline_ns) const {
        if (!valid) {
            return local_deadline_ns;
        }
        const auto drift_ratio = static_cast<long double>(estimated_drift_ppm) / 1'000'000.0L;
        const auto numerator = static_cast<long double>(local_deadline_ns) -
                               static_cast<long double>(last_offset_ns) +
                               drift_ratio * static_cast<long double>(last_time_ns);
        const auto estimate = numerator / (1.0L + drift_ratio);
        if (estimate <= static_cast<long double>(std::numeric_limits<TimeNs>::min())) {
            return std::numeric_limits<TimeNs>::min();
        }
        if (estimate >= static_cast<long double>(std::numeric_limits<TimeNs>::max())) {
            return std::numeric_limits<TimeNs>::max();
        }
        return static_cast<TimeNs>(std::llround(estimate));
    }

    TimeNs uncertainty(TimeNs time_ns, const SimulationConfig& config) const {
        if (!valid) {
            return std::numeric_limits<TimeNs>::max() / 8;
        }
        const auto age = std::max<TimeNs>(0, time_ns - last_time_ns);
        const auto holdover = static_cast<long double>(age) *
                              static_cast<long double>(config.drift_uncertainty_ppm) /
                              1'000'000.0L;
        return 3 * config.sync_measurement_noise_ns +
               static_cast<TimeNs>(std::ceil(holdover));
    }
};

TimeNs ceil_airtime(std::uint32_t payload_bytes,
                    std::uint64_t rate_bps,
                    TimeNs overhead_ns) {
    const auto bits = static_cast<long double>(payload_bytes) * 8.0L;
    const auto serialization = bits * 1'000'000'000.0L / static_cast<long double>(rate_bps);
    const auto total = static_cast<long double>(overhead_ns) + std::ceil(serialization);
    if (total > static_cast<long double>(std::numeric_limits<TimeNs>::max())) {
        throw std::overflow_error("configured airtime exceeds int64 nanoseconds");
    }
    return static_cast<TimeNs>(total);
}

TimeNs align_up(TimeNs value, TimeNs quantum) {
    if (value > std::numeric_limits<TimeNs>::max() - (quantum - 1)) {
        throw std::overflow_error("slot alignment exceeds int64 nanoseconds");
    }
    return ((value + quantum - 1) / quantum) * quantum;
}

TimeNs true_clock_offset(const FlowSpec& flow, TimeNs true_time_ns) {
    const auto drift = static_cast<long double>(true_time_ns) *
                       static_cast<long double>(flow.clock_drift_ppm) / 1'000'000.0L;
    return flow.clock_offset_ns + static_cast<TimeNs>(std::llround(drift));
}

std::uint32_t copies_for_flow(const SimulationConfig& config, const FlowSpec& flow) {
    return 1U +
           (flow.priority >= config.replication_priority_min ? config.extra_replicas : 0U);
}

std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '\"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '\"';
    return escaped;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '\\':
                output << "\\\\";
                break;
            case '\"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(character) << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

class SimulationEngine {
public:
    explicit SimulationEngine(SimulationConfig config)
        : config_(std::move(config)),
          channel_rng_(config_.seed ^ 0x9E3779B97F4A7C15ULL),
          clock_rng_(config_.seed ^ 0xD1B54A32D192ED03ULL),
          sync_rng_(config_.seed ^ 0x94D049BB133111EBULL),
          unit_distribution_(0.0, 1.0) {
        estimators_.resize(config_.flows.size());
    }

    SimulationResult execute() {
        schedule_inputs();
        while (!events_.empty()) {
            const auto event = events_.top();
            events_.pop();
            if (event.time_ns > config_.duration_ns) {
                break;
            }
            current_time_ns_ = event.time_ns;
            switch (event.type) {
                case InternalEventType::Sync:
                    handle_sync(event);
                    break;
                case InternalEventType::Arrival:
                    handle_arrival(event);
                    break;
                case InternalEventType::WiredComplete:
                    handle_wired_complete(event);
                    break;
                case InternalEventType::WirelessComplete:
                    handle_wireless_complete(event);
                    break;
                case InternalEventType::Tick:
                    handle_tick(event);
                    break;
            }
        }
        finalize_pending();
        result_.scenario_id = config_.scenario_id;
        result_.scheduler = config_.scheduler;
        result_.seed = config_.seed;
        result_.generated_packets = packets_.size();
        validate_result(result_);
        return result_;
    }

private:
    SimulationConfig config_;
    SimulationResult result_;
    std::priority_queue<InternalEvent, std::vector<InternalEvent>, EarlierEvent> events_;
    std::unordered_map<std::uint64_t, PacketState> packets_;
    std::deque<std::uint64_t> wired_queue_;
    std::vector<TxJob> wireless_queue_;
    std::vector<SyncEstimator> estimators_;
    std::mt19937_64 channel_rng_;
    std::mt19937_64 clock_rng_;
    std::mt19937_64 sync_rng_;
    std::uniform_real_distribution<double> unit_distribution_;
    bool channel_bad_{false};
    double channel_loss_draw_{0.0};
    TimeNs current_time_ns_{0};
    TimeNs wired_busy_until_{0};
    TimeNs wireless_busy_until_{0};
    std::uint64_t internal_sequence_{0};
    std::uint64_t log_sequence_{0};
    std::uint64_t packet_sequence_{1};
    std::uint64_t enqueue_sequence_{0};

    void push_event(InternalEvent event) {
        event.sequence = internal_sequence_++;
        events_.push(std::move(event));
    }

    void append_log(LogEvent event) {
        event.sequence = log_sequence_++;
        result_.events.push_back(std::move(event));
    }

    TimeNs sample_normal(TimeNs standard_deviation_ns, bool synchronization = false) {
        if (standard_deviation_ns == 0) {
            return 0;
        }
        const auto deviation = static_cast<double>(standard_deviation_ns);
        std::normal_distribution<double> distribution(0.0, deviation);
        auto& generator = synchronization && config_.independent_clock_noise ? sync_rng_ : clock_rng_;
        const auto bounded = std::clamp(distribution(generator), -6.0 * deviation,
                                        6.0 * deviation);
        return static_cast<TimeNs>(std::llround(bounded));
    }

    void schedule_inputs() {
        for (std::size_t flow_index = 0; flow_index < config_.flows.size(); ++flow_index) {
            for (TimeNs time = 0; time <= config_.duration_ns;
                 time += config_.sync_interval_ns) {
                InternalEvent sync;
                sync.time_ns = time;
                sync.type = InternalEventType::Sync;
                sync.flow_index = flow_index;
                push_event(sync);
                if (time > config_.duration_ns - config_.sync_interval_ns) {
                    break;
                }
            }

            const auto& flow = config_.flows[flow_index];
            const auto last_arrival = last_observable_arrival_ns(config_, flow);
            if (!last_arrival.has_value()) {
                continue;
            }
            for (TimeNs time = flow.start_ns;;) {
                const auto count = flow.kind == FlowKind::Burst ? flow.burst_size : 1U;
                for (std::uint32_t copy = 0; copy < count; ++copy) {
                    InternalEvent arrival;
                    arrival.time_ns = time;
                    arrival.type = InternalEventType::Arrival;
                    arrival.flow_index = flow_index;
                    arrival.packet_id = packet_sequence_++;
                    push_event(arrival);
                }
                const auto remaining_ns = last_arrival.value() - time;
                if (flow.period_ns > remaining_ns) {
                    break;
                }
                time += flow.period_ns;
            }
        }
        for (TimeNs time = 0; time <= config_.duration_ns; time += config_.tick_ns) {
            InternalEvent tick;
            tick.time_ns = time;
            tick.type = InternalEventType::Tick;
            push_event(tick);
            if (time > config_.duration_ns - config_.tick_ns) {
                break;
            }
        }
    }

    void handle_sync(const InternalEvent& event) {
        const auto& flow = config_.flows.at(event.flow_index);
        const auto true_offset = true_clock_offset(flow, event.time_ns);
        const bool outage = config_.sync_outage_start_ns >= 0 &&
                            event.time_ns >= config_.sync_outage_start_ns &&
                            event.time_ns < config_.sync_outage_end_ns;
        auto& estimator = estimators_.at(event.flow_index);
        if (!outage) {
            const auto measured = true_offset + sample_normal(config_.sync_measurement_noise_ns, true);
            estimator.update(event.time_ns, measured, config_);
        }
        LogEvent log;
        log.time_ns = event.time_ns;
        log.event = outage ? "SYNC_OUTAGE" : "SYNC_OBSERVE";
        log.flow_id = flow.id;
        log.outcome = outage ? "SKIPPED" : "UPDATED";
        log.true_clock_error_ns = true_offset;
        log.estimated_clock_error_ns = estimator.estimate_offset(event.time_ns);
        log.uncertainty_ns = estimator.uncertainty(event.time_ns, config_);
        log.detail = "two-way-like synthetic observation; not IEEE 802.1AS conformance";
        append_log(std::move(log));
    }

    void handle_arrival(const InternalEvent& event) {
        const auto& flow = config_.flows.at(event.flow_index);
        PacketState packet;
        packet.id = event.packet_id;
        packet.flow_index = event.flow_index;
        packet.arrival_ns = event.time_ns;
        packet.deadline_ns = event.time_ns + flow.deadline_ns;
        packet.local_deadline_ns =
            packet.deadline_ns + true_clock_offset(flow, packet.deadline_ns) +
            sample_normal(config_.timestamp_noise_ns);
        packet.priority = flow.priority;
        packet.copies_total = copies_for_flow(config_, flow);
        packets_.emplace(packet.id, packet);
        wired_queue_.push_back(packet.id);

        LogEvent log;
        log.time_ns = event.time_ns;
        log.event = "ARRIVAL";
        log.packet_id = packet.id;
        log.flow_id = flow.id;
        log.queue_depth = wired_queue_.size();
        log.deadline_ns = packet.deadline_ns;
        log.local_deadline_ns = packet.local_deadline_ns;
        log.payload_bytes = flow.payload_bytes;
        log.detail = flow.kind == FlowKind::Burst ? "burst" : "periodic";
        append_log(std::move(log));
    }

    bool gate_open(const PacketState& packet, TimeNs time_ns) const {
        const auto phase = time_ns % config_.gate_cycle_ns;
        const bool critical_window = phase < config_.gate_critical_window_ns;
        const bool critical_packet = packet.priority >= config_.critical_priority_min;
        return critical_packet == critical_window;
    }

    std::optional<std::uint64_t> select_wired_packet(TimeNs time_ns) {
        const auto iterator = std::find_if(wired_queue_.begin(), wired_queue_.end(),
                                           [&](std::uint64_t packet_id) {
                                               return gate_open(packets_.at(packet_id), time_ns);
                                           });
        if (iterator == wired_queue_.end()) {
            return std::nullopt;
        }
        const auto packet_id = *iterator;
        wired_queue_.erase(iterator);
        return packet_id;
    }

    TimeNs clock_aware_deadline(const PacketState& packet, TimeNs time_ns) const {
        const auto& estimator = estimators_.at(packet.flow_index);
        if (!estimator.valid) {
            return packet.local_deadline_ns;
        }
        const auto estimated_global = estimator.estimate_global_deadline(packet.local_deadline_ns);
        if (!config_.clock_margin_enabled) {
            return estimated_global;
        }
        const auto prediction_horizon = std::max(time_ns, estimated_global);
        const auto uncertainty = estimator.uncertainty(prediction_horizon, config_);
        if (estimated_global < std::numeric_limits<TimeNs>::min() + uncertainty) {
            return std::numeric_limits<TimeNs>::min();
        }
        return estimated_global - uncertainty;
    }

    std::optional<TxJob> select_wireless_job(TimeNs time_ns) {
        wireless_queue_.erase(
            std::remove_if(wireless_queue_.begin(), wireless_queue_.end(), [&](const TxJob& job) {
                return packets_.at(job.packet_id).terminal;
            }),
            wireless_queue_.end());
        if (wireless_queue_.empty()) {
            return std::nullopt;
        }

        auto better = [&](const TxJob& left, const TxJob& right) {
            const auto& left_packet = packets_.at(left.packet_id);
            const auto& right_packet = packets_.at(right.packet_id);
            switch (config_.scheduler) {
                case SchedulerKind::Fifo:
                    return left.enqueue_order < right.enqueue_order;
                case SchedulerKind::StrictPriority:
                    if (left_packet.priority != right_packet.priority) {
                        return left_packet.priority > right_packet.priority;
                    }
                    return left.enqueue_order < right.enqueue_order;
                case SchedulerKind::Edf:
                    if (left_packet.local_deadline_ns != right_packet.local_deadline_ns) {
                        return left_packet.local_deadline_ns < right_packet.local_deadline_ns;
                    }
                    return left.enqueue_order < right.enqueue_order;
                case SchedulerKind::ClockAwareEdf: {
                    const auto left_deadline = clock_aware_deadline(left_packet, time_ns);
                    const auto right_deadline = clock_aware_deadline(right_packet, time_ns);
                    if (left_deadline != right_deadline) {
                        return left_deadline < right_deadline;
                    }
                    if (left_packet.priority != right_packet.priority) {
                        return left_packet.priority > right_packet.priority;
                    }
                    return left.enqueue_order < right.enqueue_order;
                }
            }
            return false;
        };

        auto best = wireless_queue_.begin();
        for (auto iterator = std::next(wireless_queue_.begin()); iterator != wireless_queue_.end();
             ++iterator) {
            if (better(*iterator, *best)) {
                best = iterator;
            }
        }
        auto selected = *best;
        wireless_queue_.erase(best);
        return selected;
    }

    void advance_channel() {
        const auto transition_draw = unit_distribution_(channel_rng_);
        if (channel_bad_) {
            if (transition_draw < config_.channel_p_bad_to_good) {
                channel_bad_ = false;
            }
        } else if (transition_draw < config_.channel_p_good_to_bad) {
            channel_bad_ = true;
        }
        channel_loss_draw_ = unit_distribution_(channel_rng_);
    }

    void maybe_start_wired(TimeNs time_ns) {
        if (time_ns < wired_busy_until_) {
            return;
        }
        const auto selected = select_wired_packet(time_ns);
        if (!selected.has_value()) {
            return;
        }
        const auto packet_id = selected.value();
        const auto& packet = packets_.at(packet_id);
        const auto& flow = config_.flows.at(packet.flow_index);
        const auto duration = ceil_airtime(flow.payload_bytes, config_.wired_rate_bps,
                                           config_.wired_overhead_ns);
        if (duration > config_.duration_ns - time_ns) {
            wired_queue_.push_front(packet_id);
            return;
        }
        const auto end_ns = time_ns + duration;
        wired_busy_until_ = end_ns;

        LogEvent log;
        log.time_ns = time_ns;
        log.event = "WIRED_TX";
        log.packet_id = packet_id;
        log.flow_id = flow.id;
        log.link = "wired";
        log.start_ns = time_ns;
        log.end_ns = end_ns;
        log.queue_depth = wired_queue_.size();
        log.deadline_ns = packet.deadline_ns;
        log.local_deadline_ns = packet.local_deadline_ns;
        log.payload_bytes = flow.payload_bytes;
        log.detail = "Qbv-inspired gate; not standards-compatible";
        append_log(std::move(log));

        InternalEvent completion;
        completion.time_ns = end_ns;
        completion.type = InternalEventType::WiredComplete;
        completion.packet_id = packet_id;
        push_event(completion);
    }

    void maybe_start_wireless(TimeNs time_ns) {
        if (time_ns < wireless_busy_until_) {
            return;
        }
        const auto selected = select_wireless_job(time_ns);
        if (!selected.has_value()) {
            return;
        }
        auto job = selected.value();
        auto& packet = packets_.at(job.packet_id);
        const auto& flow = config_.flows.at(packet.flow_index);
        ++job.attempts;
        const auto raw_duration = ceil_airtime(flow.payload_bytes, config_.wireless_rate_bps,
                                               config_.wireless_overhead_ns);
        const auto duration = align_up(raw_duration, config_.wireless_slot_ns);
        if (duration > config_.duration_ns - time_ns) {
            wireless_queue_.push_back(job);
            return;
        }
        const auto end_ns = time_ns + duration;
        wireless_busy_until_ = end_ns;
        const auto loss_probability =
            channel_bad_ ? config_.channel_loss_bad : config_.channel_loss_good;
        const bool success = channel_loss_draw_ >= loss_probability;
        const auto channel_state = channel_bad_ ? "BAD" : "GOOD";

        LogEvent log;
        log.time_ns = time_ns;
        log.event = "WIRELESS_TX";
        log.packet_id = packet.id;
        log.flow_id = flow.id;
        log.copy_id = job.copy_id;
        log.attempt = job.attempts;
        log.link = "wireless";
        log.start_ns = time_ns;
        log.end_ns = end_ns;
        log.queue_depth = wireless_queue_.size();
        log.channel_state = channel_state;
        log.deadline_ns = packet.deadline_ns;
        log.local_deadline_ns = packet.local_deadline_ns;
        log.payload_bytes = flow.payload_bytes;
        log.detail = "packet/slot-level airtime model";
        append_log(std::move(log));

        InternalEvent completion;
        completion.time_ns = end_ns;
        completion.type = InternalEventType::WirelessComplete;
        completion.packet_id = packet.id;
        completion.job = job;
        completion.wireless_success = success;
        completion.channel_state = channel_state;
        push_event(std::move(completion));
    }

    void handle_tick(const InternalEvent& event) {
        advance_channel();
        maybe_start_wired(event.time_ns);
        maybe_start_wireless(event.time_ns);
    }

    void handle_wired_complete(const InternalEvent& event) {
        auto& packet = packets_.at(event.packet_id);
        const auto& flow = config_.flows.at(packet.flow_index);
        LogEvent received;
        received.time_ns = event.time_ns;
        received.event = "WIRED_RX";
        received.packet_id = packet.id;
        received.flow_id = flow.id;
        received.link = "wired";
        received.outcome = "SUCCESS";
        received.deadline_ns = packet.deadline_ns;
        received.local_deadline_ns = packet.local_deadline_ns;
        received.payload_bytes = flow.payload_bytes;
        append_log(std::move(received));

        for (std::uint32_t copy_id = 0; copy_id < packet.copies_total; ++copy_id) {
            TxJob job;
            job.packet_id = packet.id;
            job.copy_id = copy_id;
            job.enqueue_order = enqueue_sequence_++;
            wireless_queue_.push_back(job);
        }
    }

    void handle_wireless_complete(const InternalEvent& event) {
        auto& packet = packets_.at(event.packet_id);
        const auto& flow = config_.flows.at(packet.flow_index);
        LogEvent received;
        received.time_ns = event.time_ns;
        received.event = "WIRELESS_RX";
        received.packet_id = packet.id;
        received.flow_id = flow.id;
        received.copy_id = event.job.copy_id;
        received.attempt = event.job.attempts;
        received.link = "wireless";
        received.channel_state = event.channel_state;
        received.outcome = event.wireless_success ? "SUCCESS" : "LOSS";
        received.deadline_ns = packet.deadline_ns;
        received.local_deadline_ns = packet.local_deadline_ns;
        received.payload_bytes = flow.payload_bytes;
        append_log(std::move(received));

        if (packet.terminal) {
            return;
        }
        if (event.wireless_success) {
            packet.terminal = true;
            ++result_.delivered_packets;
            LogEvent delivered;
            delivered.time_ns = event.time_ns;
            delivered.event = "DELIVER";
            delivered.packet_id = packet.id;
            delivered.flow_id = flow.id;
            delivered.copy_id = event.job.copy_id;
            delivered.attempt = event.job.attempts;
            delivered.outcome = event.time_ns <= packet.deadline_ns ? "ON_TIME" : "LATE";
            delivered.deadline_ns = packet.deadline_ns;
            delivered.local_deadline_ns = packet.local_deadline_ns;
            delivered.payload_bytes = flow.payload_bytes;
            append_log(std::move(delivered));
            return;
        }
        if (event.job.attempts <= config_.max_retries) {
            auto retry = event.job;
            retry.enqueue_order = enqueue_sequence_++;
            wireless_queue_.push_back(retry);
            return;
        }
        ++packet.exhausted_copies;
        if (packet.exhausted_copies >= packet.copies_total) {
            packet.terminal = true;
            ++result_.dropped_packets;
            LogEvent dropped;
            dropped.time_ns = event.time_ns;
            dropped.event = "DROP";
            dropped.packet_id = packet.id;
            dropped.flow_id = flow.id;
            dropped.outcome = "RETRY_AND_REPLICA_BUDGET_EXHAUSTED";
            dropped.deadline_ns = packet.deadline_ns;
            dropped.local_deadline_ns = packet.local_deadline_ns;
            dropped.payload_bytes = flow.payload_bytes;
            append_log(std::move(dropped));
        }
    }

    void finalize_pending() {
        std::vector<std::uint64_t> packet_ids;
        packet_ids.reserve(packets_.size());
        for (const auto& [packet_id, packet] : packets_) {
            if (!packet.terminal) {
                packet_ids.push_back(packet_id);
            }
        }
        std::sort(packet_ids.begin(), packet_ids.end());
        for (const auto packet_id : packet_ids) {
            auto& packet = packets_.at(packet_id);
            packet.terminal = true;
            ++result_.dropped_packets;
            const auto& flow = config_.flows.at(packet.flow_index);
            LogEvent dropped;
            dropped.time_ns = config_.duration_ns;
            dropped.event = "DROP";
            dropped.packet_id = packet.id;
            dropped.flow_id = flow.id;
            dropped.outcome = "END_OF_SIMULATION";
            dropped.deadline_ns = packet.deadline_ns;
            dropped.local_deadline_ns = packet.local_deadline_ns;
            dropped.payload_bytes = flow.payload_bytes;
            append_log(std::move(dropped));
        }
    }
};

}  // namespace

Simulator::Simulator(SimulationConfig config) : config_(std::move(config)) {
    validate_config(config_);
}

SimulationResult Simulator::run() {
    return SimulationEngine(config_).execute();
}

void validate_result(const SimulationResult& result) {
    std::unordered_map<std::uint64_t, TimeNs> arrivals;
    std::set<std::uint64_t> terminals;
    std::map<std::string, TimeNs> link_ends;
    TimeNs previous_time = -1;
    std::size_t delivered = 0;
    std::size_t dropped = 0;
    for (std::size_t index = 0; index < result.events.size(); ++index) {
        const auto& event = result.events[index];
        if (event.sequence != index || event.time_ns < 0 || event.time_ns < previous_time) {
            throw std::logic_error("event log violates sequence or monotonic-time causality");
        }
        previous_time = event.time_ns;
        if (event.event == "ARRIVAL") {
            if (!arrivals.emplace(event.packet_id, event.time_ns).second) {
                throw std::logic_error("packet has multiple arrivals");
            }
        }
        if (event.event == "WIRED_TX" || event.event == "WIRELESS_TX") {
            if (event.start_ns != event.time_ns || event.end_ns <= event.start_ns) {
                throw std::logic_error("link interval is non-causal");
            }
            const auto previous_end = link_ends[event.link];
            if (event.start_ns < previous_end) {
                throw std::logic_error("link capacity overlap detected");
            }
            link_ends[event.link] = event.end_ns;
        }
        if (event.event == "DELIVER" || event.event == "DROP") {
            const auto arrival = arrivals.find(event.packet_id);
            if (arrival == arrivals.end() || event.time_ns < arrival->second ||
                !terminals.insert(event.packet_id).second) {
                throw std::logic_error("packet terminal state is missing, duplicated, or non-causal");
            }
            delivered += event.event == "DELIVER" ? 1U : 0U;
            dropped += event.event == "DROP" ? 1U : 0U;
        }
    }
    if (arrivals.size() != result.generated_packets || terminals.size() != arrivals.size() ||
        delivered != result.delivered_packets || dropped != result.dropped_packets ||
        delivered + dropped != result.generated_packets) {
        throw std::logic_error("packet conservation invariant failed");
    }
}

void write_result(const SimulationResult& result,
                  const SimulationConfig& config,
                  const std::filesystem::path& output_directory) {
    std::filesystem::create_directories(output_directory);
    const auto events_path = output_directory / "events.csv";
    const auto metadata_path = output_directory / "run_metadata.json";
    if (std::filesystem::exists(events_path) || std::filesystem::exists(metadata_path)) {
        throw std::runtime_error("refusing to overwrite an existing run log in: " +
                                 output_directory.string());
    }
    std::ofstream events(events_path, std::ios::binary);
    if (!events) {
        throw std::runtime_error("cannot write event log: " + events_path.string());
    }
    events << "sequence,time_ns,event,packet_id,flow_id,copy_id,attempt,link,start_ns,end_ns,"
              "payload_bytes,queue_depth,channel_state,outcome,deadline_ns,local_deadline_ns,"
              "true_clock_error_ns,estimated_clock_error_ns,uncertainty_ns,detail\n";
    for (const auto& event : result.events) {
        events << event.sequence << ',' << event.time_ns << ',' << csv_escape(event.event) << ','
               << event.packet_id << ',' << csv_escape(event.flow_id) << ',' << event.copy_id << ','
               << event.attempt << ',' << csv_escape(event.link) << ',' << event.start_ns << ','
               << event.end_ns << ',' << event.payload_bytes << ',' << event.queue_depth << ','
               << csv_escape(event.channel_state) << ',' << csv_escape(event.outcome) << ','
               << event.deadline_ns << ',' << event.local_deadline_ns << ','
               << event.true_clock_error_ns << ',' << event.estimated_clock_error_ns << ','
               << event.uncertainty_ns << ',' << csv_escape(event.detail) << '\n';
    }

    std::ofstream metadata(metadata_path, std::ios::binary);
    if (!metadata) {
        throw std::runtime_error("cannot write run metadata: " + metadata_path.string());
    }
    metadata << "{\n"
             << "  \"duration_ns\": " << config.duration_ns << ",\n"
             << "  \"dropped_packets\": " << result.dropped_packets << ",\n"
             << "  \"extra_replicas\": " << config.extra_replicas << ",\n"
             << "  \"flows\": [\n";
    for (std::size_t index = 0; index < config.flows.size(); ++index) {
        const auto& flow = config.flows[index];
        metadata << "    {\"copies_total\": " << copies_for_flow(config, flow)
                 << ", \"id\": \"" << json_escape(flow.id) << "\", \"priority\": "
                 << flow.priority << "}" << (index + 1U == config.flows.size() ? "\n" : ",\n");
    }
    metadata << "  ],\n"
             << "  \"generated_packets\": " << result.generated_packets << ",\n"
             << "  \"delivered_packets\": " << result.delivered_packets << ",\n"
             << "  \"max_retries\": " << config.max_retries << ",\n"
             << "  \"model_scope\": \"packet/slot-level software simulation; no PHY, hardware, "
                "openwifi, or standards-conformance claim\",\n"
             << "  \"replication_priority_min\": " << config.replication_priority_min << ",\n"
             << "  \"scenario_id\": \"" << json_escape(result.scenario_id) << "\",\n"
             << "  \"scheduler\": \"" << to_string(result.scheduler) << "\",\n"
             << "  \"seed\": " << result.seed << ",\n"
             << "  \"sync_estimator\": \"" << config.sync_estimator << "\",\n"
             << "  \"sync_window\": " << config.sync_window << ",\n"
             << "  \"sync_measurement_noise_ns\": " << config.sync_measurement_noise_ns << ",\n"
             << "  \"clock_margin_enabled\": " << (config.clock_margin_enabled ? "true" : "false") << ",\n"
             << "  \"independent_clock_noise\": " << (config.independent_clock_noise ? "true" : "false") << ",\n"
             << "  \"schema_version\": \"1.0\"\n"
             << "}\n";
}

}  // namespace wtsn
