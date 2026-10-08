#include "logger/Level.h"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace
{

using logger::Level;

TEST(Level, OrderIsOffErrorWarnInfoDebugPerf)
{
    static_assert(Level::Off < Level::Error);
    static_assert(Level::Error < Level::Warn);
    static_assert(Level::Warn < Level::Info);
    static_assert(Level::Info < Level::Debug);
    static_assert(Level::Debug < Level::Perf);
    EXPECT_EQ(static_cast<std::uint8_t>(Level::Off), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(Level::Perf), 5);
}

TEST(Level, NamesAreUpperCaseTags)
{
    EXPECT_EQ(logger::level_name(Level::Off), "OFF");
    EXPECT_EQ(logger::level_name(Level::Error), "ERROR");
    EXPECT_EQ(logger::level_name(Level::Warn), "WARN");
    EXPECT_EQ(logger::level_name(Level::Info), "INFO");
    EXPECT_EQ(logger::level_name(Level::Debug), "DEBUG");
    EXPECT_EQ(logger::level_name(Level::Perf), "PERF");
}

TEST(Level, ParseIsCaseInsensitive)
{
    EXPECT_EQ(logger::parse_level("off"), Level::Off);
    EXPECT_EQ(logger::parse_level("ERROR"), Level::Error);
    EXPECT_EQ(logger::parse_level("Warn"), Level::Warn);
    EXPECT_EQ(logger::parse_level("iNfO"), Level::Info);
    EXPECT_EQ(logger::parse_level("debug"), Level::Debug);
    EXPECT_EQ(logger::parse_level("PERF"), Level::Perf);
}

TEST(Level, ParseRejectsUnknownAndEmpty)
{
    EXPECT_EQ(logger::parse_level(""), std::nullopt);
    EXPECT_EQ(logger::parse_level("verbose"), std::nullopt);
    EXPECT_EQ(logger::parse_level("warning"), std::nullopt);
    EXPECT_EQ(logger::parse_level(" info"), std::nullopt);
    EXPECT_EQ(logger::parse_level("3"), std::nullopt);
}

TEST(Level, DefaultCompiledLevelIsPerf)
{
    static_assert(logger::kCompiledLevel == Level::Perf);
    static_assert(!logger::is_compiled_in(Level::Off));
    static_assert(logger::is_compiled_in(Level::Error));
    static_assert(logger::is_compiled_in(Level::Perf));
    EXPECT_TRUE(logger::is_compiled_in(Level::Debug));
}

}  // namespace
