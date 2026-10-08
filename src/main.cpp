// Demo of the logger library: every level, the two shipped sinks, two custom sinks and the error
// handler.
//
//     logger [level] [file]    level: error|warn|info|debug|perf|off (default: LOGGER_LEVEL or
//     info)
//                              file:  where the FileSink appends (default: <temp>/logger-demo.log)

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "logger/ConsoleSink.h"
#include "logger/FileSink.h"
#include "logger/Level.h"
#include "logger/Record.h"
#include "logger/Sink.h"
#include "logger/logger.h"

namespace
{

/// A custom sink is one class with one method. This one counts records and keeps the last line.
class CountingSink final : public logger::Sink
{
public:
    void Write(const logger::Record& record) override
    {
        ++count;
        lastLine = logger::format_line(record);
    }

    [[nodiscard]] std::size_t        Count() const noexcept { return count; }
    [[nodiscard]] const std::string& LastLine() const noexcept { return lastLine; }

private:
    std::size_t count = 0;
    std::string lastLine;
};

/// Fails on its first record, to show what happens then: the error handler is told, and the other
/// sinks still receive the record.
class FailsOnceSink final : public logger::Sink
{
public:
    void Write(const logger::Record& /*unused*/) override
    {
        if (!std::exchange(failed, true))
        {
            throw std::runtime_error("simulated disk full");
        }
    }

private:
    bool failed = false;
};

}  // namespace

int main(int argc, char* argv[])
{
    try
    {
        const std::span args(argv, static_cast<std::size_t>(argc));
        if (args.size() > 1)
        {
            // A level given in code wins over LOGGER_LEVEL from the environment.
            if (const auto level = logger::parse_level(args[1]))
            {
                logger::set_level(*level);
            }
            else
            {
                std::cerr << "usage: logger [error|warn|info|debug|perf|off] [file]\n";
                return EXIT_FAILURE;
            }
        }
        const std::filesystem::path logFile =
            args.size() > 2 ? std::filesystem::path(args[2])
                            : std::filesystem::temp_directory_path() / "logger-demo.log";

        // Failures inside the logger (a sink that throws, a bad LOGGER_LEVEL, ...) never reach the
        // caller as exceptions; they go to this handler. Without one, the first goes to stderr.
        logger::set_error_handler(
            [](std::string_view what) { std::cerr << "demo: logger reported: " << what << '\n'; });

        logger::add_sink<logger::ConsoleSink>();  // the default when no sink is added
        logger::add_sink<logger::FileSink>(logFile);
        const auto counter = logger::add_sink<CountingSink>();
        logger::add_sink<FailsOnceSink>();

        logger::error("disk {} is {:.1f}% full", "/dev/sda1", 97.3);
        logger::warn("retrying {} (attempt {})", "connect", 2);
        logger::info("listening on {}:{}", "0.0.0.0", 8080);
        logger::debug("config has {} entries", std::vector{1, 2, 3}.size());

        std::vector<int> values(100'000);
        int              seed = 0;
        for (int& value : values)
        {
            value = (seed++ * 7919) % 10'007;
        }
        const auto started = std::chrono::steady_clock::now();
        std::ranges::sort(values);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        logger::perf("sorted {} values in {} us", values.size(), elapsed.count());

        // Log calls are thread-safe; each thread's records stay in its own order.
        std::vector<std::thread> threads;
        threads.reserve(3);
        for (int t = 0; t < 3; ++t)
        {
            threads.emplace_back([t] {
                for (int i = 0; i < 3; ++i)
                {
                    logger::info("thread {} message {}", t, i);
                }
            });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }

        logger::flush();  // everything above is now in the file and on stdout
        std::cout << "demo: level " << logger::level_name(logger::current_level()) << ", "
                  << counter->Count() << " records reached the counting sink, the last one being\n"
                  << "      " << counter->LastLine() << "\ndemo: the file sink appended to "
                  << logFile.string() << '\n';

        // No flush() or shutdown() after this: the queue is drained when the program exits.
        logger::info("this record is written by the exit-time drain");
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
