# logger

[![release](https://img.shields.io/github/v/release/cthunter01/cpplogger)](https://github.com/cthunter01/cpplogger/releases/latest)
[![CI](https://github.com/cthunter01/cpplogger/actions/workflows/ci.yml/badge.svg)](https://github.com/cthunter01/cpplogger/actions/workflows/ci.yml)
![platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows%20%7C%20FreeBSD-blue)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue)

A small, thread-safe, asynchronous logging library for C++23 applications. Log calls format on the calling
thread with `std::format`, hand the finished record to one worker thread, and return; the worker writes to the
configured sinks. Five levels (`ERROR < WARN < INFO < DEBUG < PERF`) with one strict threshold, compile-time
checked format strings, call-site file and line via `std::source_location`, and no macros.

```cpp
#include "logger/FileSink.h"
#include "logger/logger.h"

int main()
{
    logger::add_sink<logger::FileSink>("app.log");   // stdout is the default when no sink is added
    logger::set_level(logger::Level::Debug);         // or LOGGER_LEVEL=debug in the environment

    logger::info("listening on {}:{}", host, port);  // 2026-10-07T12:34:56.789Z [INFO ] main.cpp:9 listening on ...
    logger::debug("state = {}", state);
    logger::perf("request handled in {} us", elapsed.count());
}
```

## Using the library

Add the repository with `add_subdirectory` or `FetchContent` and link `logger::lib`:

```cmake
include(FetchContent)
FetchContent_Declare(logger GIT_REPOSITORY https://github.com/cthunter01/logger.git GIT_TAG v0.1.0)
FetchContent_MakeAvailable(logger)
target_link_libraries(my_app PRIVATE logger::lib)   # brings the include path, C++23 and Threads::Threads
```

The library's own tests, demo and docs are built only when it is the top-level project (`LOGGER_BUILD_TESTS`,
`LOGGER_BUILD_DEMO`, `LOGGER_BUILD_DOCS`), so a consumer gets the `logger_lib` target and nothing else.

### API (`logger/logger.h`)

| Call                                                               | What it does                                                                                                                                                                    |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `logger::error/warn/info/debug/perf(fmt, args...)`                 | Logs at that level. `fmt` is checked at compile time like `std::format`; the arguments are formatted on the calling thread. Never throws.                                       |
| `logger::log(level, fmt, args...)`                                 | The same with a level chosen at run time (for wrappers).                                                                                                                        |
| `logger::set_level(level)`, `current_level()`, `is_enabled(level)` | The threshold. A record is written when its level is at or below it; `Level::Off` silences everything. Default `Info`.                                                          |
| `logger::add_sink(sink)`, `add_sink<S>(args...)`, `clear_sinks()`  | Sinks receive every record in the order they were added. With none configured, a `ConsoleSink` writes to stdout. `clear_sinks()` flushes first.                                 |
| `logger::set_error_handler(handler)`                               | Receives one message per failure (a sink that threw, a message whose formatter threw, a bad `LOGGER_LEVEL`, ...). The default prints the first one to stderr, then stays quiet. |
| `logger::set_queue_capacity(n)`                                    | The queue bound, default 8192 records. When the queue is full, log calls block until there is room, so none of them is dropped.                                                 |
| `logger::flush()`                                                  | Returns once every record logged before the call has been written to all sinks and the sinks flushed.                                                                           |
| `logger::shutdown()`                                               | Drains, flushes and joins the worker. Optional: the same happens when the program exits (`main` returning or `std::exit`). The next log call starts a new worker.               |

### Configuration

- **`LOGGER_LEVEL`** (environment): `error`, `warn`, `info`, `debug`, `perf` or `off`, any case, read once at the
  logger's first use. `set_level()` in code wins. An unknown value is reported through the error handler and
  ignored.
- **`LOGGER_MIN_LEVEL`** (compile time): calls above this level compile to nothing, no formatting and no queueing
  (the arguments are still evaluated). Set the CMake cache variable `LOGGER_MIN_LEVEL` to `OFF`, `ERROR`, `WARN`,
  `INFO`, `DEBUG` or `PERF`, which defines the macro for the library and every consumer, or define
  `LOGGER_MIN_LEVEL=LOGGER_LEVEL_INFO` (or a number 0..5) yourself, for a whole target or for single source
  files: each translation unit keeps the level it was compiled with. `logger::kCompiledLevel` and
  `logger::is_compiled_in(level)` tell at compile time what is in. With the CMake variable set, the library's own
  unit tests are not built (they assume every level is present); the demo still runs as a test.

### Sinks (`logger/Sink.h`)

A sink is one class with one method. `Write()` and `Flush()` are only ever called by one thread at a time, so
it needs no locking. Throw to report a failure; the logger reports it and carries on with the next sink.

```cpp
class StderrSink final : public logger::Sink
{
public:
    void Write(const logger::Record& record) override
    {
        std::cerr << logger::format_line(record) << '\n';   // the standard layout, or build your own
    }
};
logger::add_sink<StderrSink>();
```

`ConsoleSink` (stdout) and `FileSink` (append to a path, no rotation) are included. `logger/Record.h` has the
record type and the layout helpers (`format_line`, `format_line_to`, `format_timestamp`, `file_basename`).
The standard line is `<UTC timestamp, ms> [<level, 5 wide>] <file basename>:<line> <message>`.

### Guarantees and limits

- Log calls never throw and block only while the queue is full. Each thread's records are written in the order
  that thread logged them. A record is stamped when it is queued, so timestamps never run backwards in the output.
- A sink or the error handler may log: such records are queued without blocking, and dropped (and reported) if
  the queue is full or after the exit-time shutdown. `flush()` and `shutdown()` called from inside a sink return
  at once.
- Everything queued is written when the program exits normally, and static destructors can still log afterwards
  (their records are written synchronously). Queued records are lost on `std::quick_exit`, `_Exit`, `abort` or a
  crash, and when `std::exit` is called from inside a sink or the error handler; call `flush()` first where that
  matters.
- `fork()`: the child has no worker thread. Call `shutdown()` before forking; the next log call in either
  process starts a worker again. A child that only calls `exec` or `_exit` is unaffected.
- `ConsoleSink` writes to `std::cout` from the worker thread. A program that calls
  `std::ios::sync_with_stdio(false)` must not use `std::cout` from its own threads while a `ConsoleSink` is
  active.
- If `logger::lib` ends up inside a Windows DLL, call `shutdown()` before `main` returns: the exit-time join
  would otherwise run under the loader lock.

To see how the library is put together, [`docs/rebuild-guide/`](docs/rebuild-guide/README.md) rebuilds it from an
empty directory in 14 steps, each of which compiles and passes its tests.

## Requirements

- CMake 3.28+ and Ninja
- A C++23 compiler with `<format>`, `std::source_location` and `consteval`:
  - Linux: GCC 14+ or Clang 18+
  - macOS: Xcode 16.3+ or its Command Line Tools (Apple Clang 17+)
  - Windows: Visual Studio 2022 17.7+ (MSVC) with the "Desktop development with C++" workload
  - FreeBSD: 15+, with the base system's Clang (`pkg install cmake-core ninja git-lite` for the rest)
- Optional: clang-tidy, clang-format, llvm-cov/llvm-profdata (coverage), Doxygen (docs), ccache.
  On macOS, clang-tidy comes from Homebrew (`brew install llvm`). Coverage uses Xcode's llvm-cov.

GoogleTest is used from the system when installed, otherwise downloaded at configure time.

## Build

Linux, macOS and FreeBSD:

```sh
cmake --workflow --preset dev          # configure + build + test, Clang Debug
./build/clang-debug/bin/logger
```

Windows, from a **Developer PowerShell for VS** (Ninja needs MSVC's environment; VS Code's CMake Tools and
Visual Studio set it up themselves):

```powershell
cmake --workflow --preset dev-msvc     # configure + build + test, MSVC Debug
.\build\msvc-debug\bin\logger.exe
```

| Preset                                     | Platforms             | What it is                                                                            |
| ------------------------------------------ | --------------------- | ------------------------------------------------------------------------------------- |
| `clang-debug`, `clang-release`             | Linux, macOS, FreeBSD | Everyday builds (Apple Clang on macOS)                                                |
| `gcc-debug`, `gcc-release`                 | Linux                 | Everyday builds                                                                       |
| `msvc-debug`, `msvc-release`               | Windows               | Everyday builds                                                                       |
| `asan`                                     | Linux, macOS          | Clang Debug with AddressSanitizer + UndefinedBehaviorSanitizer                        |
| `tsan`                                     | Linux, macOS          | Clang RelWithDebInfo with ThreadSanitizer                                             |
| `tidy`                                     | Linux, macOS          | Clang Debug running clang-tidy on every file; findings are errors                     |
| `coverage`                                 | Linux, macOS          | `cmake --workflow --preset coverage` writes `build/coverage/coverage/html/index.html` |
| `ci-gcc`, `ci-clang`, `ci-msvc`            | as their compiler     | Release builds with warnings as errors, as run in CI                                  |
| `dist-linux`, `dist-macos`, `dist-windows` | Linux, macOS, Windows | The release archives (see [Releases](#releases))                                      |

A preset exists only on the platforms it supports; `cmake --list-presets` shows the ones for this machine.
Each workflow preset (`dev`, `dev-msvc`, `ci-gcc`, `ci-clang`, `ci-msvc`, `asan`, `tsan`, `tidy`, `coverage`)
configures, builds and tests in one command, and the `dist-*` ones also package. Separate steps:
`cmake --preset <p>`, `cmake --build --preset <p>`, `ctest --preset <p>`.

CI (GitHub Actions) builds and tests on all four: Linux (`ci-gcc`, `ci-clang`, `asan`, `tidy`), macOS
(`ci-clang`), Windows (`ci-msvc`) and FreeBSD (`ci-clang`, in a VM). The sanitizer, tidy and coverage presets are
not checked on FreeBSD.

The demo executable (`src/main.cpp`) exercises every level, both sinks, two custom sinks and the error handler:
`./build/clang-debug/bin/logger [level] [file]`.

API docs: `cmake --build --preset clang-debug --target docs`, then open `build/clang-debug/docs/html/index.html`.

## Releases

The Release workflow (`.github/workflows/release.yml`) runs only when started by hand, never on a push:

1. Raise `VERSION` in `project()` in `CMakeLists.txt`, then commit and push.
2. Start it from the Actions tab (Release > Run workflow, pick the branch) or with `gh workflow run release.yml`
   (`-f prerelease=true` marks it a pre-release).

It stops at once if the tag `v<version>` already exists. Otherwise it runs all of CI and builds, tests and
packages an archive on each platform. Only when every job passes does it tag the commit `v<version>` and
publish a GitHub release with the archives, a `SHA256SUMS` file and generated release notes.

| Archive                                   | Built with                            | Usable with                                     |
| ----------------------------------------- | ------------------------------------- | ----------------------------------------------- |
| `logger-<version>-linux-x86_64.tar.gz`    | GCC 14, Ubuntu 24.04                  | the same major GCC and libstdc++ ABI            |
| `logger-<version>-macos-universal.tar.gz` | Apple Clang                           | Apple Clang, macOS 14+, Apple silicon and Intel |
| `logger-<version>-windows-x86_64.zip`     | MSVC, Release, static runtime (`/MT`) | MSVC Release builds using `/MT`                 |

An archive holds what the `install()` rules install: `include/logger/*.h` and the static library. A static C++
library only links into builds that use the same compiler family, standard library and (on Windows) runtime
library, so `add_subdirectory`/`FetchContent` (above) is the supported way to consume it; the archives are a
convenience. Build one locally with `cmake --workflow --preset dist-linux` (or `dist-macos`, `dist-windows`); it
lands in `build/dist-<os>/package/`.
