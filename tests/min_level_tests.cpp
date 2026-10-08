// Compiled with LOGGER_MIN_LEVEL=LOGGER_LEVEL_WARN (tests/CMakeLists.txt): info(), debug() and
// perf() must compile to nothing, without even formatting their arguments.
#include <atomic>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CaptureSink.h"
#include "default_level_unit.h"
#include "logger/Level.h"
#include "logger/logger.h"

namespace
{

using logger::Level;

/// Counts how often it is formatted.
struct Probe
{
    std::atomic<int>* formatted;
};

}  // namespace

template <>
struct std::formatter<Probe>
{
    // NOLINTNEXTLINE(readability-identifier-naming): the std::formatter protocol spells it this way
    static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    // NOLINTNEXTLINE(readability-identifier-naming)
    static auto format(const Probe& probe, std::format_context& ctx)
        -> std::format_context::iterator
    {
        ++*probe.formatted;
        return std::format_to(ctx.out(), "probe");
    }
};

namespace
{

TEST(MinLevel, CompiledLevelIsWarn)
{
    static_assert(logger::kCompiledLevel == Level::Warn);
    static_assert(logger::is_compiled_in(Level::Error));
    static_assert(logger::is_compiled_in(Level::Warn));
    static_assert(!logger::is_compiled_in(Level::Info));
    static_assert(!logger::is_compiled_in(Level::Perf));
    EXPECT_EQ(LOGGER_MIN_LEVEL, LOGGER_LEVEL_WARN);
}

TEST(MinLevel, StrippedCallsDoNotFormatOrEnqueue)
{
    const auto sink = std::make_shared<logger::test::CaptureSink>();
    logger::clear_sinks();
    logger::add_sink(sink);
    logger::set_level(Level::Perf);
    std::atomic<int> formatted{0};
    const Probe      probe{&formatted};

    logger::info("{}", probe);
    logger::debug("{}", probe);
    logger::perf("{}", probe);
    logger::log(Level::Info, "{}", probe);
    logger::flush();
    EXPECT_EQ(formatted.load(), 0);
    EXPECT_EQ(sink->Count(), 0U);

    logger::warn("{}", probe);
    logger::error("{}", probe);
    logger::log(Level::Error, "{}", probe);
    logger::flush();
    EXPECT_EQ(formatted.load(), 3);
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"probe", "probe", "probe"}));
    logger::clear_sinks();
}

TEST(MinLevel, IsEnabledIsFalseAboveTheCompiledLevel)
{
    logger::set_level(Level::Perf);
    EXPECT_EQ(logger::current_level(), Level::Perf);
    EXPECT_TRUE(logger::is_enabled(Level::Warn));
    EXPECT_FALSE(logger::is_enabled(Level::Info));
    EXPECT_FALSE(logger::is_enabled(Level::Perf));
}

TEST(MinLevel, OtherTranslationUnitsKeepTheirOwnLevel)
{
    // Both units instantiate logger::info<const int&> and call logger::is_enabled; without the
    // per-level inline namespace the linker would keep one definition for both.
    const auto sink = std::make_shared<logger::test::CaptureSink>();
    logger::clear_sinks();
    logger::add_sink(sink);
    logger::set_level(Level::Perf);
    const int value = 7;

    logger::info("stripped {}", value);
    logger::test::default_unit_log_info(value);
    logger::flush();

    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"default unit 7"}));
    EXPECT_EQ(logger::test::default_unit_compiled_level(), Level::Perf);
    EXPECT_TRUE(logger::test::default_unit_info_enabled());
    EXPECT_FALSE(logger::is_enabled(Level::Info));
    logger::clear_sinks();
}

}  // namespace
