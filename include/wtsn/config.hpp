#pragma once

#include <filesystem>
#include <optional>

#include "wtsn/model.hpp"

namespace wtsn {

SimulationConfig load_config(const std::filesystem::path& path);
void validate_config(const SimulationConfig& config);
std::optional<TimeNs> last_observable_arrival_ns(const SimulationConfig& config,
                                                 const FlowSpec& flow);

}  // namespace wtsn
