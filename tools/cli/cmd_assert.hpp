#pragma once

namespace goggles::cli {

auto run_assert_image(int argc, char** argv) -> int;
auto run_assert_clean(int argc, char** argv) -> int;
auto run_assert_no_degradation(int argc, char** argv) -> int;

} // namespace goggles::cli
