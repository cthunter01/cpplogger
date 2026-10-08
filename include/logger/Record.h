#pragma once

#include <chrono>
#include <source_location>
#include <string>
#include <string_view>

#include "logger/Level.h"

namespace logger
{

/// One log event, as handed to sinks. `timestamp` is taken when the record is queued. `location`
/// points at the logger::info(...) call; its file_name() and function_name() are string literals in
/// the binary, so a Record can be copied and kept.
struct Record
{
    std::chrono::system_clock::time_point timestamp;
    Level                                 level = Level::Info;
    std::source_location                  location;
    std::string                           message;
};

/// The part of @p path after the last '/' or '\\' (whichever the compiler used), or all of it.
[[nodiscard]] constexpr std::string_view file_basename(std::string_view path) noexcept
{
    const auto pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

/// UTC ISO 8601 with milliseconds, e.g. "2026-10-07T12:34:56.789Z". Pure arithmetic: no time zone
/// database.
[[nodiscard]] std::string format_timestamp(std::chrono::system_clock::time_point timestamp);

/// Appends the standard line to @p out, without a trailing newline:
///     2026-10-07T12:34:56.789Z [INFO ] Server.cpp:142 message
/// UTC with milliseconds, the level tag padded to 5 characters, the basename of
/// location.file_name(). Custom sinks call this to share the layout (and add their own line
/// ending).
void format_line_to(std::string& out, const Record& record);

/// The standard line (see format_line_to) as a new string.
[[nodiscard]] std::string format_line(const Record& record);

}  // namespace logger
