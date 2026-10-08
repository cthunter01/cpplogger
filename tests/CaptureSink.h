#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "logger/Record.h"
#include "logger/Sink.h"

namespace logger::test
{

/// Keeps every record it receives, as a Record and as the standard line, for assertions after
/// logger::flush().
class CaptureSink final : public Sink
{
public:
    using Hook = std::function<void(const Record&)>;

    CaptureSink() = default;
    /// @p hook runs inside Write(), on the writing thread, after the record was stored (re-entrancy
    /// tests).
    explicit CaptureSink(Hook hook) : onWrite(std::move(hook)) { }

    void Write(const Record& record) override
    {
        {
            const std::scoped_lock lock(mutex);
            records.push_back(record);
            lines.push_back(format_line(record));
        }
        if (onWrite)
        {
            onWrite(record);
        }
    }

    void Flush() override
    {
        const std::scoped_lock lock(mutex);
        ++flushes;
    }

    [[nodiscard]] std::vector<Record> Records() const
    {
        const std::scoped_lock lock(mutex);
        return records;
    }

    [[nodiscard]] std::vector<std::string> Lines() const
    {
        const std::scoped_lock lock(mutex);
        return lines;
    }

    [[nodiscard]] std::vector<std::string> Messages() const
    {
        const std::scoped_lock   lock(mutex);
        std::vector<std::string> messages;
        messages.reserve(records.size());
        for (const Record& record : records)
        {
            messages.push_back(record.message);
        }
        return messages;
    }

    [[nodiscard]] std::size_t Count() const
    {
        const std::scoped_lock lock(mutex);
        return records.size();
    }

    [[nodiscard]] std::size_t FlushCount() const
    {
        const std::scoped_lock lock(mutex);
        return flushes;
    }

private:
    mutable std::mutex       mutex;
    std::vector<Record>      records;
    std::vector<std::string> lines;
    std::size_t              flushes = 0;
    Hook                     onWrite;
};

}  // namespace logger::test
