#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// Compile-time level selection. Define LOGGER_MIN_LEVEL to one of these (or to a number 0..5) to
// compile every log call above it to nothing: -DLOGGER_MIN_LEVEL=LOGGER_LEVEL_INFO removes debug()
// and perf() entirely (no formatting, no enqueue; the arguments are still evaluated). The value
// must be a single token, not an expression. The LOGGER_MIN_LEVEL CMake option defines it for the
// library and for everything that links it.
// NOLINTBEGIN(modernize-macro-to-enum,cppcoreguidelines-macro-to-enum)
#define LOGGER_LEVEL_OFF 0
#define LOGGER_LEVEL_ERROR 1
#define LOGGER_LEVEL_WARN 2
#define LOGGER_LEVEL_INFO 3
#define LOGGER_LEVEL_DEBUG 4
#define LOGGER_LEVEL_PERF 5
// NOLINTEND(modernize-macro-to-enum,cppcoreguidelines-macro-to-enum)

#ifndef LOGGER_MIN_LEVEL
#define LOGGER_MIN_LEVEL LOGGER_LEVEL_PERF  // default: nothing is stripped
#endif

// Everything LOGGER_MIN_LEVEL steers lives in an inline namespace named after the value
// (min_level_0
// .. min_level_5). Translation units compiled with different values therefore never share a
// definition, and each keeps the level it was compiled with.
#define LOGGER_DETAIL_CAT2(a, b) a##b
#define LOGGER_DETAIL_CAT(a, b) LOGGER_DETAIL_CAT2(a, b)
#define LOGGER_MIN_LEVEL_NAMESPACE LOGGER_DETAIL_CAT(min_level_, LOGGER_MIN_LEVEL)

namespace logger
{

/// Severity, in increasing verbosity. The threshold is strict: a record is written when its level
/// is less than or equal to the configured level, so Debug enables Error..Debug and Perf enables
/// everything. Off is a threshold only: no record has level Off.
enum class Level : std::uint8_t
{
    Off   = LOGGER_LEVEL_OFF,
    Error = LOGGER_LEVEL_ERROR,
    Warn  = LOGGER_LEVEL_WARN,
    Info  = LOGGER_LEVEL_INFO,
    Debug = LOGGER_LEVEL_DEBUG,
    Perf  = LOGGER_LEVEL_PERF,
};

static_assert(LOGGER_MIN_LEVEL >= LOGGER_LEVEL_OFF && LOGGER_MIN_LEVEL <= LOGGER_LEVEL_PERF,
              "LOGGER_MIN_LEVEL must be one of LOGGER_LEVEL_OFF .. LOGGER_LEVEL_PERF (0..5)");

inline namespace LOGGER_MIN_LEVEL_NAMESPACE
{

/// The most verbose level compiled into this translation unit (see LOGGER_MIN_LEVEL).
inline constexpr Level kCompiledLevel = static_cast<Level>(LOGGER_MIN_LEVEL);

/// True when calls at @p level exist in this translation unit at all.
[[nodiscard]] constexpr bool is_compiled_in(Level level) noexcept
{
    return level != Level::Off && level <= kCompiledLevel;
}

}  // namespace LOGGER_MIN_LEVEL_NAMESPACE

/// "OFF", "ERROR", "WARN", "INFO", "DEBUG" or "PERF".
[[nodiscard]] constexpr std::string_view level_name(Level level) noexcept
{
    switch (level)
    {
        case Level::Off:
            return "OFF";
        case Level::Error:
            return "ERROR";
        case Level::Warn:
            return "WARN";
        case Level::Info:
            return "INFO";
        case Level::Debug:
            return "DEBUG";
        case Level::Perf:
            return "PERF";
    }
    return "?";
}

/// Parses "off", "error", "warn", "info", "debug" or "perf", ignoring ASCII case ("Warn", "WARN"
/// and "warn" all give Level::Warn); nullopt for anything else, including surrounding whitespace.
[[nodiscard]] std::optional<Level> parse_level(std::string_view text) noexcept;

}  // namespace logger
