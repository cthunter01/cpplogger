#include "logger/FileSink.h"

#include <filesystem>
#include <format>
#include <ios>
#include <stdexcept>

#include "logger/Record.h"

namespace logger
{

FileSink::FileSink(const std::filesystem::path& filePath)
  : path(filePath), stream(filePath, std::ios::out | std::ios::app | std::ios::binary)
{
    if (!stream)
    {
        throw std::runtime_error(
            std::format("FileSink: cannot open '{}' for appending", path.string()));
    }
}

void FileSink::Write(const Record& record)
{
    line.clear();
    format_line_to(line, record);
    line.push_back('\n');
    stream.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (!stream)
    {
        stream.clear();  // report this record, but let later ones try again
        throw std::runtime_error(std::format("FileSink: writing to '{}' failed", path.string()));
    }
}

void FileSink::Flush()
{
    stream.flush();
    if (!stream)
    {
        stream.clear();
        throw std::runtime_error(std::format("FileSink: flushing '{}' failed", path.string()));
    }
}

}  // namespace logger
