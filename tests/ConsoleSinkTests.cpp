#include "logger/ConsoleSink.h"

#include <chrono>
#include <exception>
#include <ios>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

#include <gtest/gtest.h>

#include "logger/Level.h"
#include "logger/Record.h"

namespace
{

/// Points std::cout at another buffer for the lifetime of the object, then restores buffer, state
/// and exception mask.
class CoutRedirect
{
public:
    explicit CoutRedirect(std::streambuf* target)
      : previous(std::cout.rdbuf(target)), previousMask(std::cout.exceptions())
    {
    }
    CoutRedirect(const CoutRedirect&)            = delete;
    CoutRedirect(CoutRedirect&&)                 = delete;
    CoutRedirect& operator=(const CoutRedirect&) = delete;
    CoutRedirect& operator=(CoutRedirect&&)      = delete;
    ~CoutRedirect()
    {
        std::cout.exceptions(std::ios::goodbit);
        std::cout.rdbuf(previous);  // also clears the error state
        std::cout.exceptions(previousMask);
    }

private:
    std::streambuf*   previous;
    std::ios::iostate previousMask;
};

/// A destination that accepts nothing, like a closed pipe.
class RejectingBuffer final : public std::streambuf
{
protected:
    int_type overflow(int_type /*unused*/) override { return traits_type::eof(); }
};

/// The what() of the exception Write() throws for @p record, or "" when it does not throw.
std::string write_failure(logger::ConsoleSink& sink, const logger::Record& record)
{
    try
    {
        sink.Write(record);
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
    return "";
}

logger::Record make_record(const std::string& message, logger::Level level)
{
    return {
        .timestamp = std::chrono::system_clock::time_point{},
        .level     = level,
        .location  = {},
        .message   = message,
    };
}

TEST(ConsoleSink, WritesOneLinePerRecordToStdout)
{
    const auto               first  = make_record("first", logger::Level::Info);
    const auto               second = make_record("second", logger::Level::Error);
    const std::ostringstream captured;
    {
        const CoutRedirect  redirect(captured.rdbuf());
        logger::ConsoleSink sink;
        sink.Write(first);
        sink.Write(second);
        sink.Flush();
    }
    EXPECT_EQ(captured.str(),
              logger::format_line(first) + '\n' + logger::format_line(second) + '\n');
}

TEST(ConsoleSink, ReportsAStreamFailureAndRecovers)
{
    const auto          record = make_record("x", logger::Level::Info);
    logger::ConsoleSink sink;
    {
        RejectingBuffer    rejecting;
        const CoutRedirect redirect(&rejecting);
        EXPECT_EQ(write_failure(sink, record), "ConsoleSink: writing to stdout failed");
        EXPECT_TRUE(std::cout.good());  // cleared, so that later records are attempted
    }
    const std::ostringstream captured;
    {
        const CoutRedirect redirect(captured.rdbuf());
        sink.Write(record);
        sink.Flush();
    }
    EXPECT_EQ(captured.str(), logger::format_line(record) + '\n');
}

TEST(ConsoleSink, RecoversWhenTheProgramEnabledStreamExceptions)
{
    const auto          record = make_record("x", logger::Level::Info);
    logger::ConsoleSink sink;
    RejectingBuffer     rejecting;
    const CoutRedirect  redirect(&rejecting);
    std::cout.exceptions(std::ios::badbit);
    // The sink reports every failure the same way, and leaves the stream usable.
    EXPECT_EQ(write_failure(sink, record), "ConsoleSink: writing to stdout failed");
    EXPECT_TRUE(std::cout.good());
    EXPECT_EQ(write_failure(sink, record), "ConsoleSink: writing to stdout failed");
    EXPECT_TRUE(std::cout.good());
}

}  // namespace
