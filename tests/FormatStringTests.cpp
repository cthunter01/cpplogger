#include "logger/FormatString.h"

#include <format>
#include <source_location>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "logger/Record.h"

namespace
{

TEST(FormatString, CapturesCallSiteLocation)
{
    const auto                      expectedLine = std::source_location::current().line() + 1;
    const logger::FormatString<int> fmt          = "x = {}";
    EXPECT_EQ(fmt.location.line(), expectedLine);
    EXPECT_EQ(logger::file_basename(fmt.location.file_name()), "FormatStringTests.cpp");
    EXPECT_EQ(std::format(fmt.format, 42), "x = 42");
}

TEST(FormatString, AcceptsConstexprStringView)
{
    constexpr std::string_view                   kText = "{} and {}";
    const logger::FormatString<int, std::string> fmt   = kText;
    EXPECT_EQ(std::format(fmt.format, 1, std::string("two")), "1 and two");
}

TEST(FormatString, WorksWithoutArguments)
{
    const logger::FormatString<> fmt = "plain";
    EXPECT_EQ(std::format(fmt.format), "plain");
}

}  // namespace
