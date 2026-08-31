#pragma once

#include <filesystem>

#include "wtsn/model.hpp"

namespace wtsn {

class Simulator {
public:
    explicit Simulator(SimulationConfig config);
    SimulationResult run();

private:
    SimulationConfig config_;
};

void validate_result(const SimulationResult& result);
void write_result(const SimulationResult& result,
                  const SimulationConfig& config,
                  const std::filesystem::path& output_directory);

}  // namespace wtsn
