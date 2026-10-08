#pragma once

#include "logger/Level.h"

namespace logger::test
{

// Defined in default_level_unit.cpp, which is compiled without LOGGER_MIN_LEVEL, unlike the rest of
// logger_min_level_tests. Each translation unit must keep the level it was compiled with.

/// logger::kCompiledLevel as that translation unit sees it.
[[nodiscard]] Level default_unit_compiled_level() noexcept;
/// logger::is_enabled(Level::Info) as that translation unit sees it.
[[nodiscard]] bool default_unit_info_enabled() noexcept;
/// logger::info("default unit {}", value) from that translation unit.
void default_unit_log_info(const int& value) noexcept;

}  // namespace logger::test
