#include "wtsn/config.hpp"
#include "wtsn/simulator.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Callable>
void check_throws(Callable callable, const std::string& message) {
    try {
        callable();
        check(false, message);
    } catch (const std::exception&) {
        check(true, message);
    }
}

wtsn::SimulationConfig base_config() {
    wtsn::SimulationConfig config;
    config.scenario_id = "analytic";
    config.duration_ns = 5'000'000;
    config.tick_ns = 10'000;
    config.seed = 17;
    config.scheduler = wtsn::SchedulerKind::Fifo;
    config.gate_cycle_ns = 100'000;
    config.gate_critical_window_ns = 100'000;
    config.critical_priority_min = 5;
    config.wired_rate_bps = 1'000'000'000;
    config.wired_overhead_ns = 0;
    config.wireless_rate_bps = 100'000'000;
    config.wireless_overhead_ns = 0;
    config.wireless_slot_ns = 1'000;
    config.channel_p_good_to_bad = 0.0;
    config.channel_p_bad_to_good = 1.0;
    config.channel_loss_good = 0.0;
    config.channel_loss_bad = 0.0;
    config.max_retries = 0;
    config.extra_replicas = 0;
    config.replication_priority_min = 8;
    config.timestamp_noise_ns = 0;
    config.sync_interval_ns = 100'000;
    config.sync_measurement_noise_ns = 0;
    config.drift_uncertainty_ppm = 0.0;

    wtsn::FlowSpec flow;
    flow.id = "control";
    flow.kind = wtsn::FlowKind::Periodic;
    flow.start_ns = 0;
    flow.period_ns = 1'000'000;
    flow.burst_size = 1;
    flow.payload_bytes = 128;
    flow.deadline_ns = 500'000;
    flow.priority = 10;
    flow.clock_offset_ns = 25'000;
    flow.clock_drift_ppm = 10.0;
    config.flows = {flow};
    return config;
}

void test_analytic_delivery_and_invariants() {
    auto config = base_config();
    const auto result = wtsn::Simulator(config).run();
    check(result.generated_packets == 5, "periodic source generates five packets");
    check(result.delivered_packets == 5, "loss-free analytic packets are delivered");
    check(result.dropped_packets == 0, "loss-free analytic run has no drops");
    wtsn::validate_result(result);

    const auto delivered = std::find_if(result.events.begin(), result.events.end(),
                                        [](const wtsn::LogEvent& event) {
                                            return event.event == "DELIVER";
                                        });
    check(delivered != result.events.end(), "analytic run has a delivery event");
    if (delivered != result.events.end()) {
        check(delivered->time_ns == 21'000,
              "first delivery matches wired serialization plus aligned wireless airtime");
    }

    const auto sync = std::find_if(result.events.begin(), result.events.end(),
                                   [](const wtsn::LogEvent& event) {
                                       return event.event == "SYNC_OBSERVE";
                                   });
    check(sync != result.events.end(), "analytic run contains synchronization observations");
    if (sync != result.events.end()) {
        check(sync->true_clock_error_ns == sync->estimated_clock_error_ns,
              "noise-free synchronization recovers the initial offset");
    }
}

void test_closed_gate_is_a_controlled_negative() {
    auto config = base_config();
    config.scenario_id = "closed-gate-negative";
    config.gate_critical_window_ns = 0;
    const auto result = wtsn::Simulator(config).run();
    check(result.generated_packets == result.dropped_packets,
          "a closed critical gate produces the expected terminal drops");
    check(result.delivered_packets == 0, "closed gate cannot deliver critical packets");
}

void test_retry_and_replica_budget_is_bounded() {
    auto config = base_config();
    config.scenario_id = "loss-negative";
    config.duration_ns = 1'000'000;
    config.flows.front().period_ns = 2'000'000;
    config.channel_p_good_to_bad = 1.0;
    config.channel_p_bad_to_good = 0.0;
    config.channel_loss_good = 1.0;
    config.channel_loss_bad = 1.0;
    config.max_retries = 1;
    config.extra_replicas = 1;
    config.replication_priority_min = 1;
    const auto result = wtsn::Simulator(config).run();
    check(result.generated_packets == 1, "loss fixture has one packet");
    check(result.dropped_packets == 1, "all-loss channel terminates in one packet drop");
    const auto attempts = std::count_if(result.events.begin(), result.events.end(),
                                        [](const wtsn::LogEvent& event) {
                                            return event.event == "WIRELESS_TX";
                                        });
    check(attempts == 4, "two copies each receive one bounded retry");
}

void test_config_fixture_and_validation() {
    const auto source = std::filesystem::path(WTSN_SOURCE_DIR);
    const auto config = wtsn::load_config(source / "tests" / "fixtures" / "analytic.cfg");
    check(config.scenario_id == "analytic-fixture", "fixture config is parsed");
    check(config.flows.size() == 2, "fixture config contains periodic and burst flows");
    check(config.flows.at(1).kind == wtsn::FlowKind::Burst, "burst flow kind is retained");

    auto invalid = config;
    invalid.channel_loss_good = 1.1;
    check_throws([&]() { wtsn::validate_config(invalid); },
                 "out-of-range channel probability is rejected");
    invalid = config;
    invalid.max_retries = 101;
    check_throws([&]() { wtsn::validate_config(invalid); },
                 "unbounded retry work is rejected");
    invalid = config;
    invalid.flows.resize(1);
    invalid.flows.front().start_ns = 1;
    invalid.flows.front().deadline_ns = invalid.duration_ns;
    check_throws([&]() { wtsn::validate_config(invalid); },
                 "configuration with no fully observable packets is rejected");
}

void test_duplicate_terminal_is_rejected() {
    auto result = wtsn::Simulator(base_config()).run();
    auto terminal = *std::find_if(result.events.begin(), result.events.end(),
                                  [](const wtsn::LogEvent& event) {
                                      return event.event == "DELIVER";
                                  });
    terminal.sequence = result.events.size();
    terminal.time_ns = result.events.back().time_ns;
    result.events.push_back(terminal);
    check_throws([&]() { wtsn::validate_result(result); },
                 "duplicate packet terminal state is rejected");
}

void test_clock_aware_edf_corrects_local_deadline_order() {
    auto config = base_config();
    config.scenario_id = "clock-order";
    config.duration_ns = 2'000'000;
    config.tick_ns = 10'000;
    config.gate_critical_window_ns = config.gate_cycle_ns;
    config.wireless_rate_bps = 100'000'000;
    config.sync_interval_ns = 1'000'000;
    config.flows.clear();

    auto warmup = base_config().flows.front();
    warmup.id = "warmup";
    warmup.period_ns = 3'000'000;
    warmup.payload_bytes = 2'500;
    warmup.deadline_ns = 1'500'000;
    warmup.clock_offset_ns = 0;
    warmup.clock_drift_ppm = 0.0;
    auto earlier = warmup;
    earlier.id = "earlier";
    earlier.start_ns = 10'000;
    earlier.payload_bytes = 128;
    earlier.deadline_ns = 500'000;
    earlier.clock_offset_ns = 300'000;
    auto later = earlier;
    later.id = "later";
    later.deadline_ns = 700'000;
    later.clock_offset_ns = -300'000;
    config.flows = {warmup, earlier, later};

    config.scheduler = wtsn::SchedulerKind::Edf;
    const auto raw_edf = wtsn::Simulator(config).run();
    config.scheduler = wtsn::SchedulerKind::ClockAwareEdf;
    const auto corrected_edf = wtsn::Simulator(config).run();
    const auto first_contender = [](const wtsn::SimulationResult& result) {
        return std::find_if(result.events.begin(), result.events.end(),
                            [](const wtsn::LogEvent& event) {
                                return event.event == "WIRELESS_TX" && event.flow_id != "warmup";
                            });
    };
    const auto raw_choice = first_contender(raw_edf);
    const auto corrected_choice = first_contender(corrected_edf);
    check(raw_choice != raw_edf.events.end() && raw_choice->flow_id == "later",
          "raw local-deadline EDF follows the deliberately inverted local order");
    check(corrected_choice != corrected_edf.events.end() && corrected_choice->flow_id == "earlier",
          "clock-aware EDF recovers the earlier global deadline without reading ground truth");
}

void test_observation_horizon_excludes_right_censored_arrivals() {
    auto config = base_config();
    config.scenario_id = "observation-horizon";
    config.duration_ns = 5'000'000;
    config.flows.front().period_ns = 1'000'000;
    config.flows.front().deadline_ns = 2'000'000;
    const auto result = wtsn::Simulator(config).run();

    std::vector<wtsn::TimeNs> arrivals;
    for (const auto& event : result.events) {
        if (event.event == "ARRIVAL") {
            arrivals.push_back(event.time_ns);
            check(event.deadline_ns <= config.duration_ns,
                  "every generated packet has a fully observable deadline");
        }
    }
    check(result.generated_packets == 4,
          "arrival whose deadline lies beyond the horizon is excluded");
    check(std::find(arrivals.begin(), arrivals.end(), 4'000'000) == arrivals.end(),
          "right-censored final-window arrival is not in the denominator");
    check(std::find(arrivals.begin(), arrivals.end(), 3'000'000) != arrivals.end(),
          "arrival whose deadline equals the horizon remains observable");
}

void test_packet_budget_uses_the_observation_horizon() {
    auto config = base_config();
    config.scenario_id = "observable-budget";
    auto fully_censored = config.flows.front();
    fully_censored.id = "fully-censored-high-rate";
    fully_censored.start_ns = 1;
    fully_censored.period_ns = 1;
    fully_censored.deadline_ns = config.duration_ns;
    config.flows.push_back(fully_censored);

    const auto result = wtsn::Simulator(config).run();
    check(result.generated_packets == 5,
          "fully censored high-rate source consumes no packet budget and generates no packets");
    check(std::none_of(result.events.begin(), result.events.end(),
                       [](const wtsn::LogEvent& event) {
                           return event.event == "ARRIVAL" &&
                                  event.flow_id == "fully-censored-high-rate";
                       }),
          "packet budget estimator and generator share the same observation window");
}

}  // namespace

int main() {
    test_analytic_delivery_and_invariants();
    test_closed_gate_is_a_controlled_negative();
    test_retry_and_replica_budget_is_bounded();
    test_config_fixture_and_validation();
    test_duplicate_terminal_is_rejected();
    test_clock_aware_edf_corrects_local_deadline_order();
    test_observation_horizon_excludes_right_censored_arrivals();
    test_packet_budget_uses_the_observation_horizon();
    if (failures != 0) {
        std::cerr << failures << " C++ checks failed\n";
        return 1;
    }
    std::cout << "all C++ checks passed\n";
    return 0;
}
