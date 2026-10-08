#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "logger/Record.h"
#include "logger/Sink.h"

namespace logger
{

/// Appends one line per record to a file in the standard layout. No rotation. Lines end in '\n' on
/// every platform (the file is opened in binary mode). The file stays open for the sink's lifetime.
class FileSink final : public Sink
{
public:
    /// Opens @p filePath for appending, creating the file (not its directory) if needed.
    /// Throws std::runtime_error when the file cannot be opened.
    explicit FileSink(const std::filesystem::path& filePath);

    /// Throws std::runtime_error when the stream reports a failure; the error state is cleared
    /// first, so later records are still attempted.
    void Write(const Record& record) override;
    /// Flushes the stream; throws std::runtime_error on failure, after clearing the error state.
    void Flush() override;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path; }

private:
    std::filesystem::path path;
    std::ofstream         stream;
    std::string           line;  // reused between records
};

}  // namespace logger
