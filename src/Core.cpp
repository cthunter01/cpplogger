#include "Core.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "logger/ConsoleSink.h"
#include "logger/Level.h"
#include "logger/Record.h"
#include "logger/Sink.h"
#include "logger/logger.h"

namespace logger::detail
{

namespace
{

/// One locked stdio call per message. Not std::cerr: it is tied to std::cout, which the ConsoleSink
/// may be writing on another thread.
void write_stderr(std::string_view text) noexcept
{
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
}

/// The handler used while none is installed: the first message goes to stderr, later ones are
/// dropped.
void default_error_handler(std::string_view message) noexcept
{
    static std::atomic<bool> s_reported{false};
    if (s_reported.exchange(true))
    {
        return;
    }
    try
    {
        write_stderr(
            std::format("logger: {}\nlogger: further errors are not shown; install a "
                        "handler with logger::set_error_handler\n",
                        message));
    }
    catch (...)
    {
        write_stderr("logger: error (details lost)\n");
    }
}

/// Joins the worker. A failure (which the standard allows only for a self-join, excluded by the
/// caller) is reported and the thread is let go, rather than terminating the program when the
/// handle is destroyed.
void join_worker(std::thread& worker) noexcept
{
    try
    {
        worker.join();
    }
    catch (...)
    {
        default_error_handler("could not join the worker thread; queued records may be lost");
        try
        {
            worker.detach();
        }
        catch (...)
        {
            write_stderr("logger: could not detach the worker thread\n");
        }
    }
}

}  // namespace

Core& Core::Instance() noexcept
{
    // Only constant-initialised statics here (MSVC's C4640 flags dynamically initialised local
    // statics). The object lives in static storage and is never destroyed: static destructors
    // elsewhere may still log.
    alignas(Core) static std::array<std::byte, sizeof(Core)> s_storage;
    static std::once_flag                                    s_once;
    static Core*                                             s_instance = nullptr;
    try
    {
        std::call_once(s_once, [] {
            s_instance = new (s_storage.data()) Core;  // NOLINT(cppcoreguidelines-owning-memory)
            // Runs at exit before the destructors of statics constructed earlier, which may log.
            static_cast<void>(std::atexit([] { Instance().Stop(true); }));
        });
    }
    catch (...)
    {
        // std::call_once itself failed; the initialiser cannot throw. Nothing is left to log with.
        write_stderr("logger: cannot initialise the logger\n");
        std::terminate();
    }
    return *s_instance;
}

Core::Core() noexcept
{
    try
    {
        // Read once, during the guarded first initialisation, before any logger thread exists.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char* const raw = std::getenv("LOGGER_LEVEL");
        if (raw == nullptr)
        {
            return;
        }
        if (const auto parsed = parse_level(raw))
        {
            threshold.store(*parsed, std::memory_order_relaxed);
        }
        else
        {
            startupError = std::format(
                "LOGGER_LEVEL=\"{}\" is not one of error, warn, info, debug, "
                "perf or off; the level stays {}",
                raw, level_name(threshold.load(std::memory_order_relaxed)));
        }
    }
    catch (...)
    {
        // Out of memory while building the complaint: the variable counts as unset.
        startupError.reset();
    }
}

// ---- configuration ----

void Core::SetLevel(Level newLevel) noexcept
{
    threshold.store(newLevel, std::memory_order_relaxed);
}

Level Core::GetLevel() const noexcept
{
    return threshold.load(std::memory_order_relaxed);
}

void Core::SetQueueCapacity(std::size_t newCapacity) noexcept
{
    try
    {
        // Only the bound changes. The queue's storage grows on demand, so any value is acceptable,
        // including one far larger than memory ("unbounded").
        const std::scoped_lock lock(mutex);
        capacity = newCapacity == 0 ? 1 : newCapacity;
        notFull.notify_all();  // a larger bound may unblock waiting producers
    }
    catch (...)
    {
        default_error_handler("set_queue_capacity failed");
    }
}

void Core::AddSink(std::shared_ptr<Sink> sink) noexcept
{
    if (!sink)
    {
        Report("add_sink: a null sink was ignored");
        return;
    }
    try
    {
        const std::scoped_lock lock(mutex);
        sinks.push_back(std::move(sink));
    }
    catch (...)
    {
        Report("out of memory: the sink was not added");
    }
}

void Core::ClearSinks() noexcept
{
    Flush();  // everything logged before this call reaches the old sinks
    try
    {
        // Destroyed after the lock is released: a sink's destructor may take time, or log.
        std::vector<std::shared_ptr<Sink>> removed;
        const std::scoped_lock             lock(mutex);
        removed.swap(sinks);
    }
    catch (...)
    {
        Report("clear_sinks failed");
    }
}

void Core::SetErrorHandler(ErrorHandler newHandler) noexcept
{
    // Destroyed after the lock is released: what the old handler captured may log when it dies.
    ErrorHandler               old;
    std::optional<std::string> pending;
    try
    {
        const std::scoped_lock lock(mutex);
        old     = std::exchange(errorHandler, std::move(newHandler));
        pending = TakeStartupErrorLocked();
    }
    catch (...)
    {
        default_error_handler("set_error_handler failed; the handler was not installed");
    }
    if (pending)
    {
        // A complaint about LOGGER_LEVEL goes to the handler that was just installed.
        Report(*pending);
    }
}

std::optional<std::string> Core::TakeStartupErrorLocked() noexcept
{
    return std::exchange(startupError, std::nullopt);
}

std::vector<std::shared_ptr<Sink>> Core::SinksLocked()
{
    if (!sinks.empty())
    {
        return sinks;
    }
    if (!defaultSink)
    {
        defaultSink = std::make_shared<ConsoleSink>();
    }
    return {defaultSink};
}

// ---- producers ----

void Core::Log(Level level, const std::source_location& location, std::string&& message) noexcept
{
    try
    {
        const bool reentrant = writer.load(std::memory_order_acquire) == std::this_thread::get_id();
        // The timestamp is taken when the record gets its place (queued, or written synchronously),
        // not here: a caller that has to wait for room would otherwise carry a stale one, and lines
        // would come out with time running backwards.
        Record record{
            .timestamp = {},
            .level     = level,
            .location  = location,
            .message   = std::move(message),
        };
        std::optional<std::string> startFailure;
        std::optional<std::string> startup;
        std::unique_lock           lock(mutex);
        if (reentrant)
        {
            EnqueueReentrantLocked(std::move(record));
            return;
        }
        for (;;)
        {
            switch (state)
            {
                case State::Stopped:
                    // Becomes Running, or Exiting when no thread could be created.
                    startFailure = StartWorkerLocked();
                    startup      = TakeStartupErrorLocked();
                    continue;
                case State::Running:
                    if (queue.size() < capacity)
                    {
                        record.timestamp = std::chrono::system_clock::now();
                        queue.push_back(std::move(record));
                        ++enqueued;
                        lock.unlock();
                        notEmpty.notify_one();
                        break;
                    }
                    notFull.wait(lock, [this] {
                        return queue.size() < capacity || state != State::Running;
                    });
                    continue;
                case State::Draining:
                    progress.wait(lock, [this] { return state != State::Draining; });
                    continue;
                case State::Exiting:
                    lock.unlock();
                    WriteSynchronously(std::move(record));
                    break;
            }
            break;
        }
        if (startFailure)
        {
            Report(*startFailure);
        }
        if (startup)
        {
            Report(*startup);
        }
    }
    catch (...)
    {
        default_error_handler("a log call failed (out of memory?); the record was dropped");
    }
}

/// Called on the thread that is inside the sinks: it must neither wait for room nor re-enter the
/// sinks.
void Core::EnqueueReentrantLocked(Record&& record) noexcept
{
    const bool workerAlive = state == State::Running || state == State::Draining;
    if (!workerAlive || queue.size() >= capacity)
    {
        ++dropped;
        return;
    }
    try
    {
        record.timestamp = std::chrono::system_clock::now();
        queue.push_back(std::move(record));  // the worker takes it with its next batch
        ++enqueued;
    }
    catch (...)
    {
        ++dropped;  // out of memory
    }
}

std::optional<std::string> Core::StartWorkerLocked() noexcept
{
    try
    {
        // `worker` is never joinable here: it is default-constructed, or Stop() moved from it.
        worker = std::thread([this] { Run(); });
        state  = State::Running;
        return std::nullopt;
    }
    catch (const std::exception& e)
    {
        state = State::Exiting;  // terminal: records are written synchronously from now on
        progress.notify_all();
        try
        {
            return std::format(
                "cannot start the worker thread ({}); records are written synchronously", e.what());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}

void Core::WriteSynchronously(Record&& record) noexcept
{
    try
    {
        std::vector<std::shared_ptr<Sink>> targets;
        {
            const std::scoped_lock lock(mutex);
            targets = SinksLocked();
        }
        Record moved = std::move(record);
        {
            const std::scoped_lock write(writeMutex);
            writer.store(std::this_thread::get_id(), std::memory_order_release);
            moved.timestamp = std::chrono::system_clock::now();  // write order is time order
            WriteBatch(std::span<const Record>{&moved, 1}, targets);
            FlushSinks(targets);
            writer.store(std::thread::id{}, std::memory_order_release);
        }
        ReportDrops();
    }
    catch (...)
    {
        default_error_handler(
            "a synchronous write failed (out of memory?); the record was dropped");
    }
}

// ---- the worker ----

void Core::Run() noexcept
{
    writer.store(std::this_thread::get_id(), std::memory_order_release);
    std::vector<Record> batch;
    for (;;)
    {
        try
        {
            if (!RunOnce(batch))
            {
                break;
            }
        }
        catch (...)
        {
            default_error_handler("the worker thread hit an exception (out of memory?)");
        }
    }
    writer.store(std::thread::id{}, std::memory_order_release);
}

/// One batch: waits for records, takes the whole queue, writes it, flushes the sinks, publishes
/// progress. Returns false when the queue is empty and Stop() has asked the worker to finish.
bool Core::RunOnce(std::vector<Record>& batch)
{
    std::vector<std::shared_ptr<Sink>> targets;
    bool                               haveTargets = true;
    {
        std::unique_lock lock(mutex);
        notEmpty.wait(lock, [this] { return !queue.empty() || state == State::Draining; });
        if (queue.empty())
        {
            // Draining and nothing left; the sinks were flushed after the previous batch.
            return false;
        }
        try
        {
            targets = SinksLocked();
        }
        catch (...)
        {
            // Out of memory: this batch is dropped, but counted, so that flush() cannot hang.
            haveTargets = false;
        }
        // O(1): the whole queue becomes this batch, and the queue gets the batch's old storage.
        batch.swap(queue);
        inFlight = batch.size();
        notFull.notify_all();
    }
    if (haveTargets)
    {
        // Record-major: each record goes to every sink, in the order the sinks were added.
        WriteBatch(batch, targets);
        FlushSinks(targets);
    }
    const std::size_t count = batch.size();
    batch.clear();
    {
        const std::scoped_lock lock(mutex);
        // Only now, after the sinks were flushed: that is what flush() promises.
        written += count;
        inFlight = 0;
        progress.notify_all();
    }
    if (!haveTargets)
    {
        Report("out of memory: a batch of records was dropped");
    }
    ReportDrops();
    return true;
}

void Core::WriteBatch(std::span<const Record>                   batch,
                      const std::vector<std::shared_ptr<Sink>>& targets) noexcept
{
    for (const Record& record : batch)
    {
        for (const auto& sink : targets)
        {
            try
            {
                sink->Write(record);
            }
            catch (const std::exception& e)
            {
                ReportSinkFailure("write", e.what());
            }
            catch (...)
            {
                ReportSinkFailure("write", "unknown exception");
            }
        }
    }
}

void Core::FlushSinks(const std::vector<std::shared_ptr<Sink>>& targets) noexcept
{
    for (const auto& sink : targets)
    {
        try
        {
            sink->Flush();
        }
        catch (const std::exception& e)
        {
            ReportSinkFailure("flush", e.what());
        }
        catch (...)
        {
            ReportSinkFailure("flush", "unknown exception");
        }
    }
}

void Core::ReportSinkFailure(std::string_view operation, std::string_view what) noexcept
{
    try
    {
        Report(std::format("a sink failed to {}: {}", operation, what));
    }
    catch (...)
    {
        Report("a sink failed (details lost)");
    }
}

void Core::ReportDrops() noexcept
{
    std::uint64_t newDrops = 0;
    try
    {
        const std::scoped_lock lock(mutex);
        newDrops        = dropped - droppedReported;
        droppedReported = dropped;
    }
    catch (...)
    {
        return;  // reported with the next batch
    }
    if (newDrops == 0)
    {
        return;
    }
    try
    {
        Report(
            std::format("{} record(s) logged from inside a sink or the error handler were "
                        "dropped: the queue was full or the logger was exiting",
                        newDrops));
    }
    catch (...)
    {
        Report("records logged from inside a sink or the error handler were dropped");
    }
}

// ---- flush, shutdown, errors ----

void Core::Flush() noexcept
{
    if (writer.load(std::memory_order_acquire) == std::this_thread::get_id())
    {
        return;  // from inside a sink or the error handler: waiting for ourselves would deadlock
    }
    std::optional<std::string> startup;
    try
    {
        std::unique_lock lock(mutex);
        startup = TakeStartupErrorLocked();
        // Every record logged before this call has a lower number.
        const std::uint64_t target = enqueued;
        progress.wait(lock, [this, target] {
            // Stopped and Exiting are only entered with an empty queue: nothing to wait for.
            return written >= target || state == State::Stopped || state == State::Exiting;
        });
    }
    catch (...)
    {
        default_error_handler("flush failed");
    }
    if (startup)
    {
        Report(*startup);
    }
}

void Core::Stop(bool forExit) noexcept
{
    if (writer.load(std::memory_order_acquire) == std::this_thread::get_id())
    {
        // Called from inside a sink or the error handler: the worker cannot join itself.
        if (forExit)
        {
            AbandonQueueAtExit();
        }
        return;
    }
    try
    {
        std::unique_lock lock(mutex);
        for (;;)
        {
            switch (state)
            {
                case State::Running:
                {
                    state              = State::Draining;
                    std::thread toJoin = std::move(worker);
                    notEmpty.notify_all();  // the worker finishes the queue, then exits
                    notFull.notify_all();   // blocked producers move to the Draining wait
                    lock.unlock();
                    join_worker(toJoin);
                    lock.lock();
                    state = State::Stopped;
                    progress.notify_all();
                    continue;  // same critical section: no restart can slip in before forExit acts
                }
                case State::Draining:
                    // Another Stop() is joining.
                    progress.wait(lock, [this] { return state != State::Draining; });
                    continue;
                case State::Stopped:
                    if (forExit)
                    {
                        state = State::Exiting;
                        progress.notify_all();
                    }
                    return;
                case State::Exiting:
                    return;
            }
        }
    }
    catch (...)
    {
        default_error_handler("shutdown failed");
    }
}

/// std::exit was called from inside a sink or the error handler, i.e. on the thread that writes to
/// the sinks. That thread never returns to its work, so what is queued cannot be written any more.
/// Say so on stderr (the handler may be what called std::exit), release everybody who is waiting,
/// and let other threads write synchronously from now on.
void Core::AbandonQueueAtExit() noexcept
{
    std::size_t lost = 0;
    try
    {
        const std::scoped_lock lock(mutex);
        if (state == State::Exiting)
        {
            return;
        }
        lost = queue.size() + inFlight;
        queue.clear();
        written  = enqueued;
        inFlight = 0;
        state    = State::Exiting;
        notEmpty.notify_all();
        notFull.notify_all();
        progress.notify_all();
    }
    catch (...)
    {
        return;
    }
    try
    {
        write_stderr(
            std::format("logger: std::exit was called from inside a sink or the error "
                        "handler; up to {} queued record(s) were not written\n",
                        lost));
    }
    catch (...)
    {
        write_stderr(
            "logger: std::exit was called from inside a sink or the error handler; queued "
            "records were not written\n");
    }
}

void Core::Report(std::string_view message) noexcept
{
    ErrorHandler handler;
    try
    {
        const std::scoped_lock lock(mutex);
        handler = errorHandler;  // a copy: user code never runs under the logger's lock
    }
    catch (...)
    {
        default_error_handler(message);
        return;
    }
    try
    {
        if (handler)
        {
            handler(message);
        }
        else
        {
            default_error_handler(message);
        }
    }
    catch (...)
    {
        default_error_handler("the error handler threw");
    }
}

}  // namespace logger::detail
