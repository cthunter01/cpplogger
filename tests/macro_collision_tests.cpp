// A consumer that includes <windows.h> (which defines ERROR) or builds with -DDEBUG must still
// compile: the enumerators are Level::Error and Level::Debug, never ALL_CAPS.
#include <type_traits>

#include <gtest/gtest.h>

// NOLINTBEGIN(cppcoreguidelines-macro-usage,modernize-macro-to-enum,cppcoreguidelines-macro-to-enum)
#define ERROR 0
#define DEBUG 1
// NOLINTEND(cppcoreguidelines-macro-usage,modernize-macro-to-enum,cppcoreguidelines-macro-to-enum)

#include "logger/ConsoleSink.h"
#include "logger/FileSink.h"
#include "logger/Level.h"
#include "logger/Sink.h"
#include "logger/logger.h"

namespace
{

TEST(MacroCollision, EnumeratorsSurviveErrorAndDebugMacros)
{
    EXPECT_EQ(logger::level_name(logger::Level::Error), "ERROR");
    EXPECT_EQ(logger::level_name(logger::Level::Debug), "DEBUG");
    EXPECT_TRUE(logger::is_compiled_in(logger::Level::Debug));
    EXPECT_EQ(ERROR + DEBUG, 1);
    static_assert(std::is_base_of_v<logger::Sink, logger::ConsoleSink>);
    static_assert(std::is_base_of_v<logger::Sink, logger::FileSink>);
}

}  // namespace
