#include "logger/logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CaptureSink.h"
#include "logger/Level.h"
#include "logger/Record.h"
#include "logger/Sink.h"

namespace
{

using logger::Level;
using logger::test::CaptureSink;

/// A type whose formatter throws, to exercise the format-failure path.
struct Explosive
{ };

}  // namespace

template <>
struct std::formatter<Explosive>
{
    // NOLINTNEXTLINE(readability-identifier-naming): the std::formatter protocol spells it this way
    static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    // NOLINTNEXTLINE(readability-identifier-naming)
    static auto format(const Explosive& /*unused*/, std::format_context& /*unused*/)
        -> std::format_context::iterator
    {
        throw std::runtime_error("boom");
    }
};

namespace
{

/// One process-wide logger, so every test starts by re-stating the configuration it needs, and
/// asserts only after a flush().
class LoggerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        logger::set_error_handler([this](std::string_view message) { AddError(message); });
        logger::flush();
        logger::clear_sinks();
        logger::set_level(Level::Perf);
        logger::set_queue_capacity(logger::kDefaultQueueCapacity);
        logger::add_sink(sink);
    }

    void TearDown() override
    {
        logger::flush();
        logger::clear_sinks();
        logger::set_error_handler({});
        logger::set_level(Level::Info);
    }

    void AddError(std::string_view message)
    {
        const std::scoped_lock lock(errorsMutex);
        errors.emplace_back(message);
    }

    [[nodiscard]] std::vector<std::string> Errors() const
    {
        const std::scoped_lock lock(errorsMutex);
        return errors;
    }

    std::shared_ptr<CaptureSink> sink = std::make_shared<CaptureSink>();

private:
    mutable std::mutex       errorsMutex;
    std::vector<std::string> errors;
};

/// Blocks the writing thread inside Write() until Open() is called, so tests can fill the queue.
class GateSink final : public logger::Sink
{
public:
    void Write(const logger::Record& record) override
    {
        std::unique_lock lock(mutex);
        entered = true;
        enteredCv.notify_all();
        openCv.wait(lock, [this] { return open; });
        messages.push_back(record.message);
    }

    void WaitUntilEntered()
    {
        std::unique_lock lock(mutex);
        enteredCv.wait(lock, [this] { return entered; });
    }

    void Open()
    {
        {
            const std::scoped_lock lock(mutex);
            open = true;
        }
        openCv.notify_all();
    }

    [[nodiscard]] std::vector<std::string> Messages() const
    {
        const std::scoped_lock lock(mutex);
        return messages;
    }

private:
    mutable std::mutex       mutex;
    std::condition_variable  enteredCv;
    std::condition_variable  openCv;
    bool                     entered = false;
    bool                     open    = false;
    std::vector<std::string> messages;
};

class ThrowingSink final : public logger::Sink
{
public:
    explicit ThrowingSink(bool failOnFlush) : throwOnFlush(failOnFlush) { }

    void Write(const logger::Record& /*unused*/) override
    {
        if (!throwOnFlush)
        {
            throw std::runtime_error("disk on fire");
        }
    }

    void Flush() override
    {
        if (throwOnFlush)
        {
            throw std::runtime_error("cannot flush");
        }
    }

private:
    bool throwOnFlush;
};

/// Logs when it is destroyed, as the captured state of an error handler might.
class LogsWhenDestroyed
{
public:
    LogsWhenDestroyed()                                    = default;
    LogsWhenDestroyed(const LogsWhenDestroyed&)            = delete;
    LogsWhenDestroyed(LogsWhenDestroyed&&)                 = delete;
    LogsWhenDestroyed& operator=(const LogsWhenDestroyed&) = delete;
    LogsWhenDestroyed& operator=(LogsWhenDestroyed&&)      = delete;
    ~LogsWhenDestroyed() { logger::info("handler state destroyed"); }
};

// The whole call is non-throwing, including the conversion of the literal to a FormatString.
static_assert(noexcept(logger::info("{}", 1)));
static_assert(noexcept(logger::log(Level::Info, "plain")));

void log_one_of_each()
{
    logger::error("e");
    logger::warn("w");
    logger::info("i");
    logger::debug("d");
    logger::perf("p");
}

/// What an application wrapper looks like: the call site of audit() is what gets recorded.
template <typename... Args>
void audit(logger::FormatString<std::type_identity_t<Args>...> fmt, Args&&... args)
{
    logger::log(Level::Warn, fmt, std::forward<Args>(args)...);
}

TEST_F(LoggerTest, CapturesMessageLevelAndLocation)
{
    const auto expectedLine = std::source_location::current().line() + 1;
    logger::warn("x = {}", 42);
    logger::flush();
    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].message, "x = 42");
    EXPECT_EQ(records[0].level, Level::Warn);
    EXPECT_EQ(records[0].location.line(), expectedLine);
    EXPECT_EQ(logger::file_basename(records[0].location.file_name()), "logger_tests.cpp");
    EXPECT_TRUE(sink->Lines()[0].ends_with(
        std::format("Z [WARN ] logger_tests.cpp:{} x = 42", expectedLine)));
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, ThresholdIsStrict)
{
    logger::set_level(Level::Warn);
    EXPECT_EQ(logger::current_level(), Level::Warn);
    log_one_of_each();
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"e", "w"}));
    EXPECT_TRUE(logger::is_enabled(Level::Error));
    EXPECT_TRUE(logger::is_enabled(Level::Warn));
    EXPECT_FALSE(logger::is_enabled(Level::Info));
    EXPECT_FALSE(logger::is_enabled(Level::Perf));
    EXPECT_FALSE(logger::is_enabled(Level::Off));
}

TEST_F(LoggerTest, OffSilencesEverything)
{
    logger::set_level(Level::Off);
    log_one_of_each();
    logger::flush();
    EXPECT_EQ(sink->Count(), 0U);
    EXPECT_FALSE(logger::is_enabled(Level::Error));
}

TEST_F(LoggerTest, PerfEnablesEverythingInOrder)
{
    log_one_of_each();
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"e", "w", "i", "d", "p"}));
    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 5U);
    EXPECT_EQ(records[0].level, Level::Error);
    EXPECT_EQ(records[4].level, Level::Perf);
}

TEST_F(LoggerTest, LogWithRuntimeLevel)
{
    logger::log(Level::Debug, "{} {}", "runtime", 1);
    logger::log(Level::Off, "never");
    logger::set_level(Level::Info);
    logger::log(Level::Debug, "filtered");
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"runtime 1"}));
    EXPECT_EQ(sink->Records()[0].level, Level::Debug);
}

TEST_F(LoggerTest, BracesAndNoArguments)
{
    logger::info("{{literal}} {}", 1);
    logger::info("plain");
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"{literal} 1", "plain"}));
}

TEST_F(LoggerTest, RuntimeTextGoesThroughAnArgument)
{
    const std::string text = "{not a placeholder}";
    logger::info("{}", text);
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"{not a placeholder}"}));
}

TEST_F(LoggerTest, LocationSurvivesAUserWrapper)
{
    const auto expectedLine = std::source_location::current().line() + 1;
    audit("user {}", 7);
    logger::flush();
    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].message, "user 7");
    EXPECT_EQ(records[0].level, Level::Warn);
    EXPECT_EQ(records[0].location.line(), expectedLine);
}

TEST_F(LoggerTest, MultipleSinksReceiveEveryRecordInOrder)
{
    const auto second = std::make_shared<CaptureSink>();
    logger::add_sink(second);
    logger::info("one");
    logger::info("two");
    logger::flush();
    EXPECT_EQ(sink->Lines(), second->Lines());
    EXPECT_EQ(second->Messages(), (std::vector<std::string>{"one", "two"}));
}

TEST_F(LoggerTest, ClearSinksDeliversEarlierRecordsToTheOldSinks)
{
    logger::info("before");
    logger::clear_sinks();  // flushes first
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"before"}));
    const auto fresh = std::make_shared<CaptureSink>();
    logger::add_sink(fresh);
    logger::info("after");
    logger::flush();
    EXPECT_EQ(sink->Count(), 1U);
    EXPECT_EQ(fresh->Messages(), (std::vector<std::string>{"after"}));
}

TEST_F(LoggerTest, NoSinksMeansConsole)
{
    logger::clear_sinks();
    const std::ostringstream captured;
    std::streambuf* const    previous = std::cout.rdbuf(captured.rdbuf());
    logger::info("to the console");
    logger::flush();
    std::cout.rdbuf(previous);
    EXPECT_NE(captured.str().find("Z [INFO ] logger_tests.cpp:"), std::string::npos);
    EXPECT_TRUE(captured.str().ends_with(" to the console\n"));
}

TEST_F(LoggerTest, NullSinkIsReported)
{
    logger::add_sink(nullptr);
    logger::info("still works");
    logger::flush();
    EXPECT_EQ(sink->Count(), 1U);
    ASSERT_EQ(Errors().size(), 1U);
    EXPECT_EQ(Errors()[0], "add_sink: a null sink was ignored");
}

TEST_F(LoggerTest, PerThreadOrderIsPreservedWithASmallQueue)
{
    constexpr int kThreads   = 4;
    constexpr int kPerThread = 2000;
    logger::set_queue_capacity(16);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i)
            {
                logger::info("t{} i{}", t, i);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    logger::flush();
    const auto messages = sink->Messages();
    ASSERT_EQ(messages.size(), static_cast<std::size_t>(kThreads * kPerThread));
    std::vector<int> next(kThreads, 0);
    for (const auto& message : messages)
    {
        const int   t        = message[1] - '0';
        const auto  index    = static_cast<std::size_t>(t);
        const auto& expected = std::format("t{} i{}", t, next[index]);
        ASSERT_EQ(message, expected);
        ++next[index];
    }
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, CallerBlocksWhenTheQueueIsFull)
{
    const auto gate = std::make_shared<GateSink>();
    logger::clear_sinks();
    logger::add_sink(gate);
    logger::set_queue_capacity(2);
    logger::info("first");  // the worker takes it and blocks inside Write()
    gate->WaitUntilEntered();
    std::atomic<bool> producerDone{false};
    std::thread       producer([&producerDone] {
        logger::info("a");
        logger::info("b");  // the queue is now full
        logger::info("c");  // blocks until the worker takes the next batch
        producerDone = true;
    });
    // Can only pass wrongly if the producer is extremely slow to start; it never fails wrongly.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    EXPECT_FALSE(producerDone.load());
    gate->Open();
    producer.join();
    EXPECT_TRUE(producerDone.load());
    logger::flush();
    EXPECT_EQ(gate->Messages(), (std::vector<std::string>{"first", "a", "b", "c"}));
}

TEST_F(LoggerTest, RaisingTheCapacityUnblocksAWaitingProducer)
{
    const auto gate = std::make_shared<GateSink>();
    logger::clear_sinks();
    logger::add_sink(gate);
    logger::set_queue_capacity(1);
    logger::info("first");
    gate->WaitUntilEntered();
    logger::info("second");  // fills the queue
    std::thread producer([] { logger::info("third"); });
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    logger::set_queue_capacity(4);
    producer
        .join();  // returns only because the larger bound let "third" in: the gate is still closed
    gate->Open();
    logger::flush();
    EXPECT_EQ(gate->Messages(), (std::vector<std::string>{"first", "second", "third"}));
}

TEST_F(LoggerTest, QueueCapacityZeroIsClampedToOne)
{
    logger::set_queue_capacity(0);
    for (int i = 0; i < 100; ++i)
    {
        logger::info("{}", i);
    }
    logger::flush();
    EXPECT_EQ(sink->Count(), 100U);
}

TEST_F(LoggerTest, FlushWaitsForQueuedRecordsAndFlushesSinks)
{
    const auto gate = std::make_shared<GateSink>();
    logger::add_sink(gate);
    logger::info("one");
    logger::info("two");
    logger::info("three");
    gate->WaitUntilEntered();
    std::size_t seenAtFlush = 0;
    std::thread flusher([&seenAtFlush, &gate] {
        logger::flush();
        seenAtFlush = gate->Messages().size();
    });
    gate->Open();
    flusher.join();
    EXPECT_EQ(seenAtFlush, 3U);
    EXPECT_GE(sink->FlushCount(), 1U);
}

TEST_F(LoggerTest, SinkExceptionGoesToTheHandlerAndOthersContinue)
{
    logger::add_sink(std::make_shared<ThrowingSink>(false));
    logger::info("survives");
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"survives"}));
    ASSERT_EQ(Errors().size(), 1U);
    EXPECT_EQ(Errors()[0], "a sink failed to write: disk on fire");
}

TEST_F(LoggerTest, SinkFlushExceptionIsReported)
{
    logger::add_sink(std::make_shared<ThrowingSink>(true));
    logger::info("x");
    logger::flush();
    ASSERT_GE(Errors().size(), 1U);
    EXPECT_EQ(Errors()[0], "a sink failed to flush: cannot flush");
}

TEST_F(LoggerTest, FormatterExceptionGoesToTheHandler)
{
    logger::info("{}", Explosive{});
    logger::flush();
    EXPECT_EQ(sink->Count(), 0U);
    ASSERT_EQ(Errors().size(), 1U);
    EXPECT_TRUE(Errors()[0].starts_with("formatting the message logged at logger_tests.cpp:"))
        << Errors()[0];
    EXPECT_TRUE(Errors()[0].ends_with(" failed: boom")) << Errors()[0];
}

TEST_F(LoggerTest, ErrorHandlerExceptionIsSwallowed)
{
    // The fallback report ("the error handler threw") goes to the default handler, once per
    // process.
    logger::set_error_handler(
        [](std::string_view /*unused*/) { throw std::runtime_error("handler bug"); });
    logger::add_sink(std::make_shared<ThrowingSink>(false));
    logger::info("still delivered");
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"still delivered"}));
}

TEST_F(LoggerTest, SinkMayLogWithoutDeadlock)
{
    const auto echo = std::make_shared<CaptureSink>([](const logger::Record& record) {
        if (!record.message.starts_with("echo: "))
        {
            logger::info("echo: {}", record.message);
        }
    });
    logger::add_sink(echo);
    logger::info("original");
    logger::flush();  // covers "original"
    logger::flush();  // covers the echo, queued while the first batch was being written
    EXPECT_EQ(echo->Messages(), (std::vector<std::string>{"original", "echo: original"}));
    EXPECT_EQ(sink->Messages(), echo->Messages());
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, FlushAndShutdownFromInsideASinkReturn)
{
    const auto reentrant = std::make_shared<CaptureSink>([](const logger::Record& /*unused*/) {
        logger::flush();
        logger::shutdown();
    });
    logger::add_sink(reentrant);
    logger::info("x");
    logger::flush();
    EXPECT_EQ(reentrant->Messages(), (std::vector<std::string>{"x"}));
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, ReentrantRecordsAreDroppedAndReportedWhenTheQueueIsFull)
{
    logger::set_queue_capacity(1);
    const auto noisy = std::make_shared<CaptureSink>([](const logger::Record& record) {
        if (record.message == "trigger")
        {
            logger::info("echo 1");  // the queue was emptied by the batch swap: this one fits
            logger::info("echo 2");  // the queue is full and the writer must not wait: dropped
        }
    });
    logger::add_sink(noisy);
    logger::info("trigger");
    logger::flush();
    logger::flush();
    EXPECT_EQ(noisy->Messages(), (std::vector<std::string>{"trigger", "echo 1"}));
    ASSERT_EQ(Errors().size(), 1U);
    EXPECT_TRUE(Errors()[0].starts_with("1 record(s) logged from inside a sink")) << Errors()[0];
}

TEST_F(LoggerTest, ShutdownDrainsIsIdempotentAndRestarts)
{
    for (int i = 0; i < 50; ++i)
    {
        logger::info("{}", i);
    }
    logger::shutdown();
    EXPECT_EQ(sink->Count(), 50U);  // without any flush()
    EXPECT_GE(sink->FlushCount(), 1U);
    logger::shutdown();  // nothing to do
    logger::info("after");
    logger::flush();
    EXPECT_EQ(sink->Count(), 51U);
    EXPECT_EQ(sink->Messages().back(), "after");
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, ConcurrentShutdownCallsAreLossless)
{
    constexpr int            kPerProducer = 500;
    std::vector<std::thread> threads;
    threads.reserve(6);
    for (int p = 0; p < 2; ++p)
    {
        threads.emplace_back([p] {
            for (int i = 0; i < kPerProducer; ++i)
            {
                logger::info("p{} {}", p, i);
            }
        });
    }
    for (int s = 0; s < 4; ++s)
    {
        threads.emplace_back([] { logger::shutdown(); });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    logger::flush();
    EXPECT_EQ(sink->Count(), 2U * kPerProducer);
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, ConfigurationChangesWhileLogging)
{
    // Exists mainly to give ThreadSanitizer something to bite on. Records logged while no sink is
    // configured would go to std::cout, so it is redirected for the duration.
    const std::ostringstream discard;
    std::streambuf* const    previous = std::cout.rdbuf(discard.rdbuf());
    std::atomic<bool>        stop{false};
    std::vector<std::thread> threads;
    threads.reserve(7);
    for (int p = 0; p < 4; ++p)
    {
        threads.emplace_back([p] {
            for (int i = 0; i < 300; ++i)
            {
                logger::info("p{} {}", p, i);
            }
        });
    }
    threads.emplace_back([&stop] {
        while (!stop)
        {
            logger::set_level(Level::Debug);
            logger::set_level(Level::Perf);
            static_cast<void>(logger::current_level());
        }
    });
    threads.emplace_back([] {
        for (int i = 0; i < 10; ++i)
        {
            logger::add_sink(std::make_shared<CaptureSink>());
            logger::clear_sinks();
            logger::set_queue_capacity(8);
            logger::set_queue_capacity(logger::kDefaultQueueCapacity);
        }
    });
    threads.emplace_back([] {
        for (int i = 0; i < 20; ++i)
        {
            logger::flush();
        }
    });
    for (std::size_t i = 0; i < 4; ++i)
    {
        threads[i].join();
    }
    stop = true;
    for (std::size_t i = 4; i < threads.size(); ++i)
    {
        threads[i].join();
    }
    logger::flush();
    std::cout.rdbuf(previous);
    logger::add_sink(sink);
    for (int i = 0; i < 10; ++i)
    {
        logger::info("tail {}", i);
    }
    logger::flush();
    const auto messages = sink->Messages();
    ASSERT_GE(messages.size(), 10U);
    EXPECT_EQ(messages.back(), "tail 9");
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, AnUnreservableQueueCapacityLosesNothing)
{
    // A natural way to ask for "unbounded". The bound is only a number; storage grows on demand.
    logger::set_queue_capacity(std::numeric_limits<std::size_t>::max());
    for (int i = 0; i < 100; ++i)
    {
        logger::info("{}", i);
    }
    logger::flush();
    EXPECT_EQ(sink->Count(), 100U);
    EXPECT_TRUE(Errors().empty());
}

TEST_F(LoggerTest, TimestampsFollowQueueOrderUnderBackpressure)
{
    // With a tiny queue most callers have to wait for room. A record is stamped when it is queued,
    // not when the call started, so the written order never shows time running backwards. (Only a
    // system clock that is set back during the test could make this fail wrongly.)
    constexpr int kThreads   = 4;
    constexpr int kPerThread = 500;
    logger::set_queue_capacity(4);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i)
            {
                logger::info("t{} i{}", t, i);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    logger::flush();
    const auto records = sink->Records();
    ASSERT_EQ(records.size(), static_cast<std::size_t>(kThreads * kPerThread));
    EXPECT_TRUE(std::ranges::is_sorted(records, {}, &logger::Record::timestamp));
}

TEST_F(LoggerTest, AReplacedErrorHandlerMayLogWhenItIsDestroyed)
{
    auto state = std::make_shared<LogsWhenDestroyed>();
    logger::set_error_handler([state](std::string_view /*unused*/) { });
    state.reset();  // the handler now holds the only reference
    // Replacing the handler destroys the old one, and with it the state, which logs. That must not
    // happen under the logger's lock.
    logger::set_error_handler([this](std::string_view message) { AddError(message); });
    logger::flush();
    EXPECT_EQ(sink->Messages(), (std::vector<std::string>{"handler state destroyed"}));
    EXPECT_TRUE(Errors().empty());
}

}  // namespace
