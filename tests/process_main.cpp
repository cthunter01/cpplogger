// Process-level checks that cannot run inside one GoogleTest process: LOGGER_LEVEL (read once per
// process), the default error handler (which reports once per process), draining at exit, logging
// during static destruction, and std::exit called from inside a sink.
// Driven by tests/ProcessCheck.cmake.    Usage: logger_process_tests <scenario>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "logger/Level.h"
#include "logger/Record.h"
#include "logger/Sink.h"
#include "logger/logger.h"

namespace
{

class LogsOnDestruction
{
public:
    explicit LogsOnDestruction(const char* what) noexcept : text(what) { }
    LogsOnDestruction(const LogsOnDestruction&)            = delete;
    LogsOnDestruction(LogsOnDestruction&&)                 = delete;
    LogsOnDestruction& operator=(const LogsOnDestruction&) = delete;
    LogsOnDestruction& operator=(LogsOnDestruction&&)      = delete;
    ~LogsOnDestruction() { logger::info("{}", text); }

private:
    const char* text;
};

// Constructed before main, so destroyed after the logger's exit-time drain: its record is written
// synchronously, by the destructor's own thread.
const LogsOnDestruction kEarly{"early static destroyed"};

class ThrowingSink final : public logger::Sink
{
public:
    void Write(const logger::Record& /*unused*/) override
    {
        throw std::runtime_error("disk on fire");
    }
};

/// Calls std::exit from inside Write(), i.e. on the worker thread, once main has queued everything.
class ExitingSink final : public logger::Sink
{
public:
    explicit ExitingSink(const std::atomic<bool>& allQueued) : ready(&allQueued) { }

    void Write(const logger::Record& /*unused*/) override
    {
        while (!ready->load())
        {
            std::this_thread::yield();
        }
        std::exit(EXIT_SUCCESS);  // NOLINT(concurrency-mt-unsafe): that is the scenario
    }

private:
    const std::atomic<bool>* ready;
};

void print_level()
{
    std::cout << "level=" << logger::level_name(logger::current_level()) << '\n';
}

void drain_at_exit(bool callExit)
{
    for (int i = 0; i < 200; ++i)
    {
        // To the built-in ConsoleSink; no flush() or shutdown() follows.
        logger::info("drain i={}", i);
    }
    if (callExit)
    {
        std::exit(EXIT_SUCCESS);  // NOLINT(concurrency-mt-unsafe): that is the scenario
    }
}

[[noreturn]] void exit_in_sink()
{
    std::atomic<bool> allQueued{false};
    logger::add_sink(std::make_shared<ExitingSink>(allQueued));
    logger::info("one");
    logger::info("two");
    logger::info("three");
    allQueued = true;
    for (;;)
    {
        // The worker thread ends the process; main must not return and call exit a second time.
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
}

int run(std::string_view scenario)
{
    if (scenario == "level")
    {
        print_level();
    }
    else if (scenario == "level-override")
    {
        logger::set_level(logger::Level::Error);
        print_level();
    }
    else if (scenario == "bad-env-handler")
    {
        logger::set_error_handler(
            [](std::string_view what) { std::cout << "handler: " << what << '\n'; });
        logger::flush();
        print_level();
    }
    else if (scenario == "bad-env-default")
    {
        logger::flush();  // the complaint reaches the default handler, i.e. stderr
        print_level();
    }
    else if (scenario == "default-handler")
    {
        logger::add_sink(std::make_shared<ThrowingSink>());
        logger::info("one");
        logger::info("two");
        logger::flush();
        std::cout << "done\n";
    }
    else if (scenario == "exit-drain")
    {
        drain_at_exit(false);
    }
    else if (scenario == "std-exit")
    {
        drain_at_exit(true);
    }
    else if (scenario == "exit-order")
    {
        logger::info("hello from main");
        // Registered after the logger's first use, so it runs before the logger's exit-time drain:
        // its record is queued and then drained. kEarly is destroyed after the drain.
        static_cast<void>(std::atexit([] { logger::info("late exit handler ran"); }));
    }
    else if (scenario == "exit-in-sink")
    {
        exit_in_sink();
    }
    else
    {
        std::cerr << "unknown scenario '" << scenario << "'\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[])
{
    try
    {
        const std::span args(argv, static_cast<std::size_t>(argc));
        if (args.size() != 2)
        {
            std::cerr << "usage: logger_process_tests <scenario>\n";
            return EXIT_FAILURE;
        }
        return run(args[1]);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
