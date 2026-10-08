#pragma once

#include "logger/Record.h"

namespace logger
{

/// Where records go. Write() and Flush() are called by one thread at a time (normally the logger's
/// worker thread; after the exit-time shutdown, the logging thread itself), never concurrently with
/// each other or with another sink, so a sink needs no locking of its own. Flush() runs after every
/// batch of records, i.e. whenever the queue has been drained, and at shutdown.
///
/// Report failure by throwing: the exception goes to the error handler (see
/// logger::set_error_handler) and the logger carries on with the next sink and the next record.
///
/// A sink may log. A record logged from inside Write() or Flush() is queued without blocking; it is
/// dropped (and reported) if the queue is full, or after the exit-time shutdown, when records are
/// written synchronously. flush() and shutdown() called from inside a sink return immediately.
/// A sink must not log unconditionally for every record it receives, or it generates records
/// forever. It must not call std::exit either: what is still queued could not be written.
class Sink
{
public:
    Sink()                       = default;
    Sink(const Sink&)            = delete;
    Sink(Sink&&)                 = delete;
    Sink& operator=(const Sink&) = delete;
    Sink& operator=(Sink&&)      = delete;
    virtual ~Sink()              = default;

    /// Writes one record. Use format_line_to()/format_line() for the standard layout.
    virtual void Write(const Record& record) = 0;
    /// Pushes buffered output to its destination. Default: nothing to do.
    virtual void Flush() { }
};

}  // namespace logger
