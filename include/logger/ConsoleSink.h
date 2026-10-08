#pragma once

#include <string>

#include "logger/Record.h"
#include "logger/Sink.h"

namespace logger
{

/// Writes every record, whatever its level, to std::cout as one line in the standard layout. Used
/// automatically while no sink is configured.
///
/// It writes from the logger's worker thread and shares std::cout with the program. The standard
/// streams may be used from several threads only while they are synchronised with stdio, which is
/// the default: a program that calls std::ios::sync_with_stdio(false) must not use std::cout from
/// its own threads while a ConsoleSink is active.
class ConsoleSink final : public Sink
{
public:
    /// Throws std::runtime_error when std::cout reports a failure (the stream is cleared first).
    void Write(const Record& record) override;
    /// Flushes std::cout; throws std::runtime_error on failure.
    void Flush() override;

private:
    std::string line;  // reused between records; only the writing thread touches it
};

}  // namespace logger
