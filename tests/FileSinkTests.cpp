#include "logger/FileSink.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "logger/Level.h"
#include "logger/Record.h"

namespace
{

class FileSinkTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // A directory of its own: the same test may be running from another build tree right now.
        const auto* const info = testing::UnitTest::GetInstance()->current_test_info();
        directory = std::filesystem::path(testing::TempDir()) /
                    std::format("logger_tests_{:08x}_{}", std::random_device{}(), info->name());
        std::filesystem::create_directories(directory);
        path = directory / "test.log";
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    [[nodiscard]] std::string Contents() const
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    static logger::Record MakeRecord(const std::string& message)
    {
        return {
            .timestamp = std::chrono::system_clock::time_point{},
            .level     = logger::Level::Info,
            .location  = {},
            .message   = message,
        };
    }

    std::filesystem::path directory;
    std::filesystem::path path;
};

TEST_F(FileSinkTest, AppendsOneLinePerRecord)
{
    const auto first  = MakeRecord("first");
    const auto second = MakeRecord("second");
    {
        logger::FileSink sink(path);
        sink.Write(first);
        sink.Write(second);
        sink.Flush();
    }
    EXPECT_EQ(Contents(), logger::format_line(first) + '\n' + logger::format_line(second) + '\n');
}

TEST_F(FileSinkTest, AppendsToExistingContent)
{
    {
        std::ofstream out(path, std::ios::binary);
        out << "old\n";
    }
    const auto record = MakeRecord("new");
    {
        logger::FileSink sink(path);
        sink.Write(record);
    }  // the destructor closes (and flushes) the stream
    EXPECT_EQ(Contents(), "old\n" + logger::format_line(record) + '\n');
}

TEST_F(FileSinkTest, ThrowsWhenPathCannotBeOpened)
{
    const auto missing = directory / "no-such-directory" / "x.log";
    EXPECT_THROW(logger::FileSink{missing}, std::runtime_error);
}

TEST_F(FileSinkTest, RemembersThePath)
{
    const logger::FileSink sink(path);
    EXPECT_EQ(sink.Path(), path);
}

}  // namespace
