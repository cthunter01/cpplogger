#include "logger/Record.h"

#include <chrono>
#include <format>
#include <iterator>
#include <string>

#include "logger/Level.h"

namespace logger
{

namespace
{

// Calendar arithmetic rather than chrono format specifiers: identical output on every standard
// library, no locale, no time zone database.
void append_timestamp(std::string& out, std::chrono::system_clock::time_point timestamp)
{
    namespace chrono = std::chrono;
    const chrono::sys_time<chrono::milliseconds> instant =
        chrono::floor<chrono::milliseconds>(timestamp);
    const chrono::sys_days       day = chrono::floor<chrono::sys_days::duration>(instant);
    const chrono::year_month_day date{day};
    const chrono::hh_mm_ss       timeOfDay{instant - day};
    std::format_to(std::back_inserter(out), "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
                   static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                   static_cast<unsigned>(date.day()), timeOfDay.hours().count(),
                   timeOfDay.minutes().count(), timeOfDay.seconds().count(),
                   timeOfDay.subseconds().count());
}

}  // namespace

std::string format_timestamp(std::chrono::system_clock::time_point timestamp)
{
    std::string out;
    append_timestamp(out, timestamp);
    return out;
}

void format_line_to(std::string& out, const Record& record)
{
    append_timestamp(out, record.timestamp);
    std::format_to(std::back_inserter(out), " [{:<5}] {}:{} {}", level_name(record.level),
                   file_basename(record.location.file_name()), record.location.line(),
                   record.message);
}

std::string format_line(const Record& record)
{
    std::string out;
    format_line_to(out, record);
    return out;
}

}  // namespace logger
