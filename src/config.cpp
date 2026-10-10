#include "wtsn/config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace wtsn {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> fields;
    std::stringstream stream(value);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

TimeNs parse_time(const std::string& value, const std::string& label) {
    std::size_t used = 0;
    long long parsed = 0;
    try {
        parsed = std::stoll(value, &used);
    } catch (const std::exception&) {
        throw std::invalid_argument(label + " must be an integer number of nanoseconds");
    }
    if (used != value.size()) {
        throw std::invalid_argument(label + " must be an integer number of nanoseconds");
    }
    return static_cast<TimeNs>(parsed);
}

std::uint64_t parse_unsigned(const std::string& value, const std::string& label) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument(label + " must be non-negative");
    }
    std::size_t used = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &used);
    } catch (const std::exception&) {
        throw std::invalid_argument(label + " must be an unsigned integer");
    }
    if (used != value.size()) {
        throw std::invalid_argument(label + " must be an unsigned integer");
    }
    return static_cast<std::uint64_t>(parsed);
}

std::uint32_t parse_uint32(const std::string& value, const std::string& label) {
    const auto parsed = parse_unsigned(value, label);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(label + " is outside the supported uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

int parse_int(const std::string& value, const std::string& label) {
    const auto parsed = parse_time(value, label);
    if (parsed < static_cast<TimeNs>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<TimeNs>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(label + " is outside the supported integer range");
    }
    return static_cast<int>(parsed);
}

double parse_double(const std::string& value, const std::string& label) {
    std::size_t used = 0;
    double parsed = 0.0;
    try {
        parsed = std::stod(value, &used);
    } catch (const std::exception&) {
        throw std::invalid_argument(label + " must be numeric");
    }
    if (used != value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument(label + " must be finite numeric data");
    }
    return parsed;
}

FlowSpec parse_flow(const std::string& value, std::size_t line_number) {
    const auto fields = split_csv(value);
    if (fields.size() != 10) {
        throw std::invalid_argument(
            "flow on line " + std::to_string(line_number) +
            " requires 10 fields: id,kind,start,period,burst,payload,deadline,priority,offset,drift");
    }
    FlowSpec flow;
    flow.id = fields[0];
    const auto kind = upper(fields[1]);
    if (kind == "PERIODIC") {
        flow.kind = FlowKind::Periodic;
    } else if (kind == "BURST") {
        flow.kind = FlowKind::Burst;
    } else {
        throw std::invalid_argument("unknown flow kind on line " + std::to_string(line_number));
    }
    flow.start_ns = parse_time(fields[2], "flow.start_ns");
    flow.period_ns = parse_time(fields[3], "flow.period_ns");
    flow.burst_size = parse_uint32(fields[4], "flow.burst_size");
    flow.payload_bytes = parse_uint32(fields[5], "flow.payload_bytes");
    flow.deadline_ns = parse_time(fields[6], "flow.deadline_ns");
    flow.priority = parse_int(fields[7], "flow.priority");
    flow.clock_offset_ns = parse_time(fields[8], "flow.clock_offset_ns");
    flow.clock_drift_ppm = parse_double(fields[9], "flow.clock_drift_ppm");
    return flow;
}

void require_probability(double value, const std::string& label) {
    if (value < 0.0 || value > 1.0) {
        throw std::invalid_argument(label + " must be in [0, 1]");
    }
}

}  // namespace

std::string to_string(SchedulerKind scheduler) {
    switch (scheduler) {
        case SchedulerKind::Fifo:
            return "FIFO";
        case SchedulerKind::StrictPriority:
            return "STRICT_PRIORITY";
        case SchedulerKind::Edf:
            return "EDF";
        case SchedulerKind::ClockAwareEdf:
            return "CLOCK_AWARE_EDF";
    }
    throw std::invalid_argument("unknown scheduler enum");
}

SchedulerKind scheduler_from_string(const std::string& value) {
    const auto normalized = upper(trim(value));
    if (normalized == "FIFO") {
        return SchedulerKind::Fifo;
    }
    if (normalized == "STRICT_PRIORITY") {
        return SchedulerKind::StrictPriority;
    }
    if (normalized == "EDF") {
        return SchedulerKind::Edf;
    }
    if (normalized == "CLOCK_AWARE_EDF") {
        return SchedulerKind::ClockAwareEdf;
    }
    throw std::invalid_argument("unknown scheduler: " + value);
}

SimulationConfig load_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config: " + path.string());
    }

    SimulationConfig config;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::invalid_argument("config line " + std::to_string(line_number) +
                                        " requires key=value");
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "flow") {
            config.flows.push_back(parse_flow(value, line_number));
        } else if (key == "scenario_id") {
            config.scenario_id = value;
        } else if (key == "duration_ns") {
            config.duration_ns = parse_time(value, key);
        } else if (key == "tick_ns") {
            config.tick_ns = parse_time(value, key);
        } else if (key == "seed") {
            config.seed = parse_unsigned(value, key);
        } else if (key == "scheduler") {
            config.scheduler = scheduler_from_string(value);
        } else if (key == "gate_cycle_ns") {
            config.gate_cycle_ns = parse_time(value, key);
        } else if (key == "gate_critical_window_ns") {
            config.gate_critical_window_ns = parse_time(value, key);
        } else if (key == "critical_priority_min") {
            config.critical_priority_min = parse_int(value, key);
        } else if (key == "wired_rate_bps") {
            config.wired_rate_bps = parse_unsigned(value, key);
        } else if (key == "wired_overhead_ns") {
            config.wired_overhead_ns = parse_time(value, key);
        } else if (key == "wireless_rate_bps") {
            config.wireless_rate_bps = parse_unsigned(value, key);
        } else if (key == "wireless_overhead_ns") {
            config.wireless_overhead_ns = parse_time(value, key);
        } else if (key == "wireless_slot_ns") {
            config.wireless_slot_ns = parse_time(value, key);
        } else if (key == "channel_p_good_to_bad") {
            config.channel_p_good_to_bad = parse_double(value, key);
        } else if (key == "channel_p_bad_to_good") {
            config.channel_p_bad_to_good = parse_double(value, key);
        } else if (key == "channel_loss_good") {
            config.channel_loss_good = parse_double(value, key);
        } else if (key == "channel_loss_bad") {
            config.channel_loss_bad = parse_double(value, key);
        } else if (key == "max_retries") {
            config.max_retries = parse_uint32(value, key);
        } else if (key == "extra_replicas") {
            config.extra_replicas = parse_uint32(value, key);
        } else if (key == "replication_priority_min") {
            config.replication_priority_min = parse_int(value, key);
        } else if (key == "timestamp_noise_ns") {
            config.timestamp_noise_ns = parse_time(value, key);
        } else if (key == "sync_interval_ns") {
            config.sync_interval_ns = parse_time(value, key);
        } else if (key == "sync_measurement_noise_ns") {
            config.sync_measurement_noise_ns = parse_time(value, key);
        } else if (key == "sync_outage_start_ns") {
            config.sync_outage_start_ns = parse_time(value, key);
        } else if (key == "sync_outage_end_ns") {
            config.sync_outage_end_ns = parse_time(value, key);
        } else if (key == "drift_uncertainty_ppm") {
            config.drift_uncertainty_ppm = parse_double(value, key);
        } else if (key == "sync_estimator") {
            config.sync_estimator = value;
        } else if (key == "sync_window") {
            config.sync_window = parse_uint32(value, key);
        } else if (key == "clock_margin_enabled" || key == "independent_clock_noise") {
            if (value != "true" && value != "false") {
                throw std::invalid_argument(key + " must be true or false");
            }
            if (key == "clock_margin_enabled") {
                config.clock_margin_enabled = value == "true";
            } else {
                config.independent_clock_noise = value == "true";
            }
        } else {
            throw std::invalid_argument("unknown config key on line " +
                                        std::to_string(line_number) + ": " + key);
        }
    }
    validate_config(config);
    return config;
}

std::optional<TimeNs> last_observable_arrival_ns(const SimulationConfig& config,
                                                 const FlowSpec& flow) {
    if (flow.deadline_ns <= 0 || flow.start_ns < 0 || flow.deadline_ns > config.duration_ns) {
        return std::nullopt;
    }
    const auto last_arrival_ns = config.duration_ns - flow.deadline_ns;
    if (flow.start_ns > last_arrival_ns) {
        return std::nullopt;
    }
    return last_arrival_ns;
}

void validate_config(const SimulationConfig& config) {
    if ((config.sync_estimator != "two_point" && config.sync_estimator != "window_ols") ||
        config.sync_window < 2 || config.sync_window > 128) {
        throw std::invalid_argument("sync estimator must be two_point/window_ols with window 2..128");
    }
    constexpr auto safe_time_limit = std::numeric_limits<TimeNs>::max() / 8;
    constexpr std::uint64_t packet_budget = 1'000'000;
    constexpr std::uint64_t scheduled_event_budget = 5'000'000;
    if (config.scenario_id.empty()) {
        throw std::invalid_argument("scenario_id cannot be empty");
    }
    if (config.duration_ns <= 0 || config.tick_ns <= 0 || config.gate_cycle_ns <= 0 ||
        config.wireless_slot_ns <= 0 || config.sync_interval_ns <= 0) {
        throw std::invalid_argument("duration and timing intervals must be positive");
    }
    if (config.duration_ns > safe_time_limit || config.tick_ns > safe_time_limit ||
        config.gate_cycle_ns > safe_time_limit || config.wireless_slot_ns > safe_time_limit ||
        config.sync_interval_ns > safe_time_limit) {
        throw std::invalid_argument("timing value exceeds the overflow-safe range");
    }
    if (config.gate_critical_window_ns < 0 ||
        config.gate_critical_window_ns > config.gate_cycle_ns) {
        throw std::invalid_argument("gate critical window must be inside the gate cycle");
    }
    if (config.wired_rate_bps == 0 || config.wireless_rate_bps == 0 ||
        config.wired_overhead_ns < 0 || config.wireless_overhead_ns < 0 ||
        config.timestamp_noise_ns < 0 || config.sync_measurement_noise_ns < 0 ||
        config.drift_uncertainty_ppm < 0.0) {
        throw std::invalid_argument("rates must be positive and noise/overhead must be non-negative");
    }
    if (config.wired_overhead_ns > safe_time_limit ||
        config.wireless_overhead_ns > safe_time_limit ||
        config.timestamp_noise_ns > safe_time_limit / 6 ||
        config.sync_measurement_noise_ns > safe_time_limit / 6 ||
        config.drift_uncertainty_ppm > 100'000.0) {
        throw std::invalid_argument("overhead, noise, or uncertainty exceeds safe model bounds");
    }
    require_probability(config.channel_p_good_to_bad, "channel_p_good_to_bad");
    require_probability(config.channel_p_bad_to_good, "channel_p_bad_to_good");
    require_probability(config.channel_loss_good, "channel_loss_good");
    require_probability(config.channel_loss_bad, "channel_loss_bad");
    if (config.channel_loss_good > config.channel_loss_bad) {
        throw std::invalid_argument("good-state loss cannot exceed bad-state loss");
    }
    if (config.max_retries > 100 || config.extra_replicas > 16) {
        throw std::invalid_argument("retry/replica bounds exceed the supported safety limit");
    }
    if ((config.sync_outage_start_ns < 0) != (config.sync_outage_end_ns < 0) ||
        (config.sync_outage_start_ns >= 0 &&
         config.sync_outage_end_ns <= config.sync_outage_start_ns)) {
        throw std::invalid_argument("sync outage must be disabled with -1/-1 or have start < end");
    }
    if (config.flows.empty()) {
        throw std::invalid_argument("at least one flow is required");
    }
    std::set<std::string> flow_ids;
    std::uint64_t total_packets = 0;
    for (const auto& flow : config.flows) {
        if (flow.id.empty() || !flow_ids.insert(flow.id).second) {
            throw std::invalid_argument("flow identifiers must be non-empty and unique");
        }
        if (flow.start_ns < 0 || flow.start_ns >= config.duration_ns || flow.period_ns <= 0 ||
            flow.burst_size == 0 || flow.payload_bytes == 0 || flow.deadline_ns <= 0 ||
            !std::isfinite(flow.clock_drift_ppm)) {
            throw std::invalid_argument("flow timing, burst, payload, and drift fields are invalid");
        }
        if (flow.deadline_ns > safe_time_limit || flow.clock_offset_ns < -safe_time_limit ||
            flow.clock_offset_ns > safe_time_limit || std::abs(flow.clock_drift_ppm) > 100'000.0) {
            throw std::invalid_argument("flow clock or deadline exceeds safe model bounds");
        }
        std::uint64_t instances = 0;
        if (const auto last_arrival = last_observable_arrival_ns(config, flow);
            last_arrival.has_value()) {
            instances = static_cast<std::uint64_t>(
                            (last_arrival.value() - flow.start_ns) / flow.period_ns) +
                        1U;
        }
        const auto packets_per_instance =
            static_cast<std::uint64_t>(flow.kind == FlowKind::Burst ? flow.burst_size : 1U);
        if (instances > (packet_budget - total_packets) / packets_per_instance) {
            throw std::invalid_argument("configured source exceeds the one-million-packet budget");
        }
        total_packets += instances * packets_per_instance;
    }
    if (total_packets == 0) {
        throw std::invalid_argument("configuration has no fully observable packets");
    }
    const auto ticks = static_cast<std::uint64_t>(config.duration_ns / config.tick_ns) + 1U;
    const auto syncs_per_flow =
        static_cast<std::uint64_t>(config.duration_ns / config.sync_interval_ns) + 1U;
    if (ticks > scheduled_event_budget ||
        syncs_per_flow > (scheduled_event_budget - ticks) / config.flows.size()) {
        throw std::invalid_argument("tick/synchronization schedule exceeds the event budget");
    }
}

}  // namespace wtsn
