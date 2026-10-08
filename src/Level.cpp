#include "logger/Level.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace logger
{

namespace
{

constexpr char to_lower_ascii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::optional<Level> parse_level(std::string_view text) noexcept
{
    constexpr std::array kLevels{
        Level::Off, Level::Error, Level::Warn, Level::Info, Level::Debug, Level::Perf,
    };
    for (const Level level : kLevels)
    {
        if (std::ranges::equal(text, level_name(level), {}, to_lower_ascii, to_lower_ascii))
        {
            return level;
        }
    }
    return std::nullopt;
}

}  // namespace logger
