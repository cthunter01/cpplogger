#pragma once

#include <cstddef>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "logger/FormatString.h"  // IWYU pragma: export
#include "logger/Level.h"         // IWYU pragma: export

namespace logger
{

class Sink;

/// Records waiting for the worker thread. When the queue is full, ordinary log calls block until
/// there is room, so none of them is dropped. Only a record logged from inside a sink or the error
/// handler, which cannot wait, is dropped (and reported) when it finds no room (see Sink). Change
/// the bound with set_queue_capacity().
inline constexpr std::size_t kDefaultQueueCapacity = 8192;

/// Receives one message per logger failure: a sink that threw or reported a stream error, a message
/// whose formatter threw, a bad LOGGER_LEVEL value, a null sink, records from inside a sink that
/// had to be dropped. Called on the thread that hit the failure (sink failures: the worker thread),
/// never while the logger holds a lock, so it may log. Exceptions it throws are swallowed. The
/// default handler prints the first message to stderr and then stays quiet.
using ErrorHandler = std::function<void(std::string_view message)>;

// ---- configuration: thread-safe, callable before or after the first log call ----

/// Sets the threshold: records at this level or below (less verbose) are written. Wins over
/// LOGGER_LEVEL.
void set_level(Level level) noexcept;
/// The configured threshold: Info, unless the LOGGER_LEVEL environment variable (error, warn, info,
/// debug, perf or off, in any case; read once, at the logger's first use) or set_level() changed
/// it.
[[nodiscard]] Level current_level() noexcept;
/// Sets the queue bound, in records (0 is treated as 1). Takes effect at once, also for blocked
/// callers.
void set_queue_capacity(std::size_t capacity) noexcept;
/// Adds a sink; records are written to every sink in the order they were added. While no sink is
/// configured, records go to a built-in ConsoleSink (stdout). A null pointer is reported to the
/// error handler and ignored.
void add_sink(std::shared_ptr<Sink> sink) noexcept;
/// Constructs an S in place, adds it and returns it:
///     auto file = logger::add_sink<logger::FileSink>("app.log");
template <typename S, typename... Args>
std::shared_ptr<S> add_sink(Args&&... args)
{
    std::shared_ptr<S> sink = std::make_shared<S>(std::forward<Args>(args)...);
    add_sink(std::shared_ptr<Sink>(sink));
    return sink;
}
/// Delivers everything logged so far to the current sinks (a flush()), then removes them all.
/// Records then go to the built-in ConsoleSink again; use set_level(Level::Off) for silence.
void clear_sinks() noexcept;
/// Installs @p handler; an empty handler restores the default one. A pending complaint about
/// LOGGER_LEVEL is delivered to the new handler at once.
void set_error_handler(ErrorHandler handler) noexcept;
/// Blocks until every record logged before this call has been written to all sinks and the sinks
/// flushed. Returns immediately when called on the thread that is writing to the sinks, i.e. from
/// inside a sink, or from the error handler while it reports a sink failure.
void flush() noexcept;
/// Drains the queue, flushes the sinks and joins the worker thread. Optional: the same happens when
/// the program exits (main returning, or std::exit). Safe to call twice or concurrently. The next
/// log call after shutdown() starts a new worker. After the exit-time shutdown, records are written
/// synchronously on the logging thread, so static destructors can still log.
///
/// Queued records are lost on std::quick_exit, _Exit, abort or a crash, and when std::exit is
/// called from inside a sink or the error handler on the worker thread: call flush() first where it
/// matters. Call shutdown() before fork(), because the child has no worker thread (its next log
/// call starts one), and before main returns if logger::lib is linked into a Windows DLL, where the
/// exit-time join would run under the loader lock. Like flush(), shutdown() returns immediately on
/// the thread that is writing to the sinks.
void shutdown() noexcept;

// ---- logging ----
// Each function formats on the calling thread (arguments are taken by reference and never stored),
// then hands the finished message to the worker. They never throw: a failure while formatting goes
// to the error handler. Calls above LOGGER_MIN_LEVEL compile to nothing (the arguments are still
// evaluated).

namespace detail
{
void log_message(Level level, const std::source_location& location, std::string&& message) noexcept;
void report_format_failure(const std::source_location& location, std::string_view what) noexcept;

template <typename... Args>
void format_and_log(Level level, const FormatString<Args...>& fmt, Args&&... args) noexcept
{
    std::string message;
    try
    {
        message = std::format(fmt.format, std::forward<Args>(args)...);
    }
    catch (const std::exception& e)
    {
        report_format_failure(fmt.location, e.what());
        return;
    }
    catch (...)
    {
        report_format_failure(fmt.location, "unknown exception");
        return;
    }
    log_message(level, fmt.location, std::move(message));
}

inline namespace LOGGER_MIN_LEVEL_NAMESPACE
{
template <Level L, typename... Args>
void log_at([[maybe_unused]] const FormatString<Args...>& fmt,
            [[maybe_unused]] Args&&... args) noexcept
{
    if constexpr (is_compiled_in(L))
    {
        if (L <= current_level())
        {
            format_and_log<Args...>(L, fmt, std::forward<Args>(args)...);
        }
    }
}
}  // namespace LOGGER_MIN_LEVEL_NAMESPACE
}  // namespace detail

inline namespace LOGGER_MIN_LEVEL_NAMESPACE
{

/// True when a record at @p level would be written: compiled in and within current_level().
[[nodiscard]] inline bool is_enabled(Level level) noexcept
{
    return is_compiled_in(level) && level <= current_level();
}

/// Logs at a level chosen at run time (for wrappers); Level::Off and levels above LOGGER_MIN_LEVEL
/// log nothing.
template <typename... Args>
void log(Level level, FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    if (is_enabled(level))
    {
        detail::format_and_log<Args...>(level, fmt, std::forward<Args>(args)...);
    }
}

template <typename... Args>
void error(FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    detail::log_at<Level::Error, Args...>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    detail::log_at<Level::Warn, Args...>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    detail::log_at<Level::Info, Args...>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    detail::log_at<Level::Debug, Args...>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void perf(FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
{
    detail::log_at<Level::Perf, Args...>(fmt, std::forward<Args>(args)...);
}

}  // namespace LOGGER_MIN_LEVEL_NAMESPACE

}  // namespace logger
