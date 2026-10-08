#include "logger/ConsoleSink.h"

#include <ios>
#include <iostream>
#include <stdexcept>

#include "logger/Record.h"

namespace logger
{

void ConsoleSink::Write(const Record& record)
{
    line.clear();
    format_line_to(line, record);
    line.push_back('\n');
    bool failed = false;
    try
    {
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
    catch (const std::ios_base::failure&)
    {
        failed = true;  // the program enabled exceptions on std::cout
    }
    if (failed || !std::cout)
    {
        std::cout.clear();  // report this record, but let later ones try again
        throw std::runtime_error("ConsoleSink: writing to stdout failed");
    }
}

void ConsoleSink::Flush()
{
    bool failed = false;
    try
    {
        std::cout.flush();
    }
    catch (const std::ios_base::failure&)
    {
        failed = true;
    }
    if (failed || !std::cout)
    {
        std::cout.clear();
        throw std::runtime_error("ConsoleSink: flushing stdout failed");
    }
}

}  // namespace logger
