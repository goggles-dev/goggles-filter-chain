#pragma once

#include <cstdint>
#include <string_view>

namespace goggles::cli {

enum class Subcommand : uint8_t {
    process,
    validate,
    diagnose,
    assert_image,
    assert_clean,
    assert_no_degradation,
    capture,
};

struct SubcommandParse {
    Subcommand command;
    int first_arg_index;
};

inline auto parse_subcommand(int argc, char** argv) -> SubcommandParse {
    if (argc < 2) {
        return {.command = Subcommand::process, .first_arg_index = 1};
    }
    std::string_view arg = argv[1];
    if (arg == "validate") {
        return {.command = Subcommand::validate, .first_arg_index = 2};
    }
    if (arg == "diagnose") {
        return {.command = Subcommand::diagnose, .first_arg_index = 2};
    }
    if (arg == "assert-image") {
        return {.command = Subcommand::assert_image, .first_arg_index = 2};
    }
    if (arg == "assert-clean") {
        return {.command = Subcommand::assert_clean, .first_arg_index = 2};
    }
    if (arg == "assert-no-degradation") {
        return {.command = Subcommand::assert_no_degradation, .first_arg_index = 2};
    }
    if (arg == "capture") {
        return {.command = Subcommand::capture, .first_arg_index = 2};
    }
    return {.command = Subcommand::process, .first_arg_index = 1};
}

} // namespace goggles::cli
