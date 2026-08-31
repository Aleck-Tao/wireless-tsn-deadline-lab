#include "wtsn/config.hpp"
#include "wtsn/simulator.hpp"

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::filesystem::path config;
    std::filesystem::path output;
    std::optional<std::uint64_t> seed;
    std::optional<wtsn::SchedulerKind> scheduler;
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--config" && index + 1 < argc) {
            arguments.config = argv[++index];
        } else if (option == "--output" && index + 1 < argc) {
            arguments.output = argv[++index];
        } else if (option == "--seed" && index + 1 < argc) {
            arguments.seed = std::stoull(argv[++index]);
        } else if (option == "--scheduler" && index + 1 < argc) {
            arguments.scheduler = wtsn::scheduler_from_string(argv[++index]);
        } else if (option == "--help") {
            std::cout << "Usage: wtsn-sim --config FILE --output DIRECTORY "
                         "[--seed N] [--scheduler NAME]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown or incomplete argument: " + option);
        }
    }
    if (arguments.config.empty() || arguments.output.empty()) {
        throw std::invalid_argument("--config and --output are required");
    }
    return arguments;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        auto config = wtsn::load_config(arguments.config);
        if (arguments.seed.has_value()) {
            config.seed = arguments.seed.value();
        }
        if (arguments.scheduler.has_value()) {
            config.scheduler = arguments.scheduler.value();
        }
        wtsn::validate_config(config);
        wtsn::Simulator simulator(config);
        const auto result = simulator.run();
        wtsn::write_result(result, config, arguments.output);
        std::cout << "scenario=" << result.scenario_id
                  << " scheduler=" << wtsn::to_string(result.scheduler)
                  << " seed=" << result.seed << " generated=" << result.generated_packets
                  << " delivered=" << result.delivered_packets
                  << " dropped=" << result.dropped_packets << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
