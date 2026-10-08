#include "logger/Record.h"

#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "logger/Level.h"

namespace
{

using logger::Level;

std::chrono::system_clock::time_point noon_ish()
{
    namespace chrono = std::chrono;
    return chrono::sys_days{chrono::year{2026} / 10 / 7} + chrono::hours{12} + chrono::minutes{34} +
           chrono::seconds{56} + chrono::milliseconds{789};
}

TEST(Record, FormatTimestampIsUtcIso8601WithMilliseconds)
{
    EXPECT_EQ(logger::format_timestamp(noon_ish()), "2026-10-07T12:34:56.789Z");
    EXPECT_EQ(logger::format_timestamp(std::chrono::system_clock::time_point{}),
              "1970-01-01T00:00:00.000Z");
}

TEST(Record, FormatTimestampTruncatesBelowMilliseconds)
{
    EXPECT_EQ(logger::format_timestamp(noon_ish() + std::chrono::microseconds{999}),
              "2026-10-07T12:34:56.789Z");
    EXPECT_EQ(logger::format_timestamp(noon_ish() + std::chrono::milliseconds{211}),
              "2026-10-07T12:34:57.000Z");
}

TEST(Record, FileBasenameHandlesBothSeparators)
{
    static_assert(logger::file_basename("C:\\src\\Server.cpp") == "Server.cpp");
    static_assert(logger::file_basename("/home/a/b/Server.cpp") == "Server.cpp");
    static_assert(logger::file_basename("mixed\\dir/Server.cpp") == "Server.cpp");
    static_assert(logger::file_basename("Server.cpp") == "Server.cpp");
    static_assert(logger::file_basename("dir/").empty());
    static_assert(logger::file_basename("").empty());
    EXPECT_EQ(logger::file_basename("/x/y.cpp"), "y.cpp");
}

TEST(Record, FormatLineHasTheStandardLayout)
{
    const auto           here = std::source_location::current();
    const logger::Record record{
        .timestamp = noon_ish(),
        .level     = Level::Warn,
        .location  = here,
        .message   = "hello there",
    };
    const std::string expected =
        "2026-10-07T12:34:56.789Z [WARN ] RecordTests.cpp:" + std::to_string(here.line()) +
        " hello there";
    EXPECT_EQ(logger::format_line(record), expected);
}

TEST(Record, LevelTagsArePaddedToFiveColumns)
{
    for (const auto& [level, tag] : {
             std::pair{Level::Error, std::string_view{"[ERROR]"}},
             std::pair{Level::Warn, std::string_view{"[WARN ]"}},
             std::pair{Level::Info, std::string_view{"[INFO ]"}},
             std::pair{Level::Debug, std::string_view{"[DEBUG]"}},
             std::pair{Level::Perf, std::string_view{"[PERF ]"}},
         })
    {
        const logger::Record record{
            .timestamp = noon_ish(),
            .level     = level,
            .location  = {},
            .message   = "m",
        };
        EXPECT_NE(logger::format_line(record).find(tag), std::string::npos) << tag;
    }
}

TEST(Record, FormatLineToAppends)
{
    std::string          out = "prefix ";
    const logger::Record record{
        .timestamp = noon_ish(),
        .level     = Level::Info,
        .location  = {},
        .message   = "m",
    };
    logger::format_line_to(out, record);
    EXPECT_TRUE(out.starts_with("prefix 2026-10-07T12:34:56.789Z [INFO ] "));
    EXPECT_TRUE(out.ends_with(" m"));
}

}  // namespace
