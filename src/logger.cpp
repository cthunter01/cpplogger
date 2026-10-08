#include "logger/logger.h"

#include <cstddef>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include "Core.h"
#include "logger/Level.h"
#include "logger/Record.h"

namespace logger
{

namespace detail
{

void log_message(Level level, const std::source_location& location, std::string&& message) noexcept
{
    Core::Instance().Log(level, location, std::move(message));
}

void report_format_failure(const std::source_location& location, std::string_view what) noexcept
{
    try
    {
        Core::Instance().Report(std::format("formatting the message logged at {}:{} failed: {}",
                                            file_basename(location.file_name()), location.line(),
                                            what));
    }
    catch (...)
    {
        Core::Instance().Report("formatting a log message failed (details lost)");
    }
}

}  // namespace detail

void set_level(Level level) noexcept
{
    detail::Core::Instance().SetLevel(level);
}

Level current_level() noexcept
{
    return detail::Core::Instance().GetLevel();
}

void set_queue_capacity(std::size_t capacity) noexcept
{
    detail::Core::Instance().SetQueueCapacity(capacity);
}

void add_sink(std::shared_ptr<Sink> sink) noexcept
{
    detail::Core::Instance().AddSink(std::move(sink));
}

void clear_sinks() noexcept
{
    detail::Core::Instance().ClearSinks();
}

void set_error_handler(ErrorHandler handler) noexcept
{
    detail::Core::Instance().SetErrorHandler(std::move(handler));
}

void flush() noexcept
{
    detail::Core::Instance().Flush();
}

void shutdown() noexcept
{
    detail::Core::Instance().Stop(false);
}

}  // namespace logger
