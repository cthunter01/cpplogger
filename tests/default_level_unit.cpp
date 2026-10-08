// Compiled with the default LOGGER_MIN_LEVEL and linked into logger_min_level_tests, whose other
// source strips everything above WARN.
#include "default_level_unit.h"

#include "logger/Level.h"
#include "logger/logger.h"

namespace logger::test
{

Level default_unit_compiled_level() noexcept
{
    return logger::kCompiledLevel;
}

bool default_unit_info_enabled() noexcept
{
    return logger::is_enabled(Level::Info);
}

void default_unit_log_info(const int& value) noexcept
{
    logger::info("default unit {}", value);
}

}  // namespace logger::test
