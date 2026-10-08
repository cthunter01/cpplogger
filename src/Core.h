#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "logger/Level.h"
#include "logger/Record.h"
#include "logger/Sink.h"
#include "logger/logger.h"

namespace logger::detail
{

/// The one logger: threshold, sinks, error handler, and a bounded queue drained by one worker
/// thread.
///
/// Instance() creates it on first use, in static storage, and never destroys it, so log calls are
/// valid at any time, including from other objects' static destructors. An atexit handler
/// registered at the same moment drains the queue, joins the worker and switches to synchronous
/// writes (state Exiting).
///
/// Locking: `mutex` guards the queue, the counters, the state, the sinks and the handler. No user
/// code (a sink, the error handler, a destructor of either) ever runs while it is held.
class Core
{
public:
    static Core& Instance() noexcept;

    Core(const Core&)            = delete;
    Core(Core&&)                 = delete;
    Core& operator=(const Core&) = delete;
    Core& operator=(Core&&)      = delete;

    void                SetLevel(Level newLevel) noexcept;
    [[nodiscard]] Level GetLevel() const noexcept;
    void                SetQueueCapacity(std::size_t newCapacity) noexcept;
    void                AddSink(std::shared_ptr<Sink> sink) noexcept;
    void                ClearSinks() noexcept;
    void                SetErrorHandler(ErrorHandler newHandler) noexcept;
    /// Queues the record, blocking while the queue is full; after the exit-time Stop() it writes
    /// the record synchronously instead. Called from inside a sink or the error handler it never
    /// blocks.
    void Log(Level level, const std::source_location& location, std::string&& message) noexcept;
    void Flush() noexcept;
    /// Drains and joins. forExit additionally enters the terminal Exiting state.
    void Stop(bool forExit) noexcept;
    /// Invokes the error handler (or the default one). Never called with `mutex` held.
    void Report(std::string_view message) noexcept;

private:
    enum class State : std::uint8_t
    {
        Stopped,   // no worker; the next record starts one
        Running,   // the worker is alive
        Draining,  // Stop() is joining: the worker finishes the queue, producers wait
        Exiting,   // terminal, after the exit-time Stop(): the caller writes, under writeMutex
    };

    Core() noexcept;
    ~Core() = default;

    void                                             Run() noexcept;
    bool                                             RunOnce(std::vector<Record>& batch);
    [[nodiscard]] std::optional<std::string>         StartWorkerLocked() noexcept;
    [[nodiscard]] std::optional<std::string>         TakeStartupErrorLocked() noexcept;
    [[nodiscard]] std::vector<std::shared_ptr<Sink>> SinksLocked();
    void EnqueueReentrantLocked(Record&& record) noexcept;
    void WriteSynchronously(Record&& record) noexcept;
    void AbandonQueueAtExit() noexcept;
    void WriteBatch(std::span<const Record>                   batch,
                    const std::vector<std::shared_ptr<Sink>>& targets) noexcept;
    void FlushSinks(const std::vector<std::shared_ptr<Sink>>& targets) noexcept;
    void ReportSinkFailure(std::string_view operation, std::string_view what) noexcept;
    void ReportDrops() noexcept;

    std::atomic<Level> threshold{Level::Info};
    /// The thread currently inside the sinks: the worker while it runs, or a synchronous writer. A
    /// log call from that thread (a sink or the error handler logging) must never wait for the
    /// sinks.
    std::atomic<std::thread::id> writer;

    std::mutex              mutex;     // guards everything from here down to startupError
    std::condition_variable notEmpty;  // the worker waits: records queued, or Draining
    std::condition_variable notFull;   // producers wait: room in the queue, or state != Running
    std::condition_variable
                        progress;  // flush()/Stop()/Draining waiters: `written` or `state` changed
    std::vector<Record> queue;
    std::size_t         capacity        = kDefaultQueueCapacity;
    std::size_t         inFlight        = 0;  // records in the batch the worker is writing
    std::uint64_t       enqueued        = 0;  // records ever queued
    std::uint64_t       written         = 0;  // records written, with the sinks flushed afterwards
    std::uint64_t       dropped         = 0;  // records from inside a sink that could not be queued
    std::uint64_t       droppedReported = 0;
    State               state           = State::Stopped;
    std::thread         worker;
    std::vector<std::shared_ptr<Sink>> sinks;
    std::shared_ptr<Sink>              defaultSink;   // the ConsoleSink used while `sinks` is empty
    ErrorHandler                       errorHandler;  // empty: the default handler
    std::optional<std::string>         startupError;  // a bad LOGGER_LEVEL, reported when possible

    std::mutex writeMutex;  // serialises synchronous writers in Exiting; never held with `mutex`
};

}  // namespace logger::detail
