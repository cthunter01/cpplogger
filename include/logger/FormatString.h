#pragma once

#include <concepts>
#include <format>
#include <source_location>
#include <string_view>

namespace logger
{

/// A compile-time checked format string plus the location of the call that supplied it.
///
/// The log functions take `FormatString<std::type_identity_t<Args>...>` as their first parameter.
/// `type_identity_t` makes that parameter a non-deduced context, so `Args...` is deduced from the
/// arguments alone (exactly as `std::format` is declared) and the string literal is then implicitly
/// converted here. The converting constructor is `consteval`, so the conversion happens at compile
/// time: `std::format_string` checks the placeholders against `Args...`, and
/// `std::source_location::current()` in the default argument is evaluated where the default
/// argument is used, i.e. at the `logger::info(...)` call. Consequence: the format string must be a
/// constant expression (a literal or a constexpr std::string_view); runtime text goes through an
/// argument: `logger::info("{}", text)`.
template <typename... Args>
struct FormatString
{
    std::format_string<Args...> format;
    std::source_location        location;

    // Implicit by design, like std::format_string itself: `logger::info("x = {}", x)` must just
    // work.
    template <typename S>
        requires std::convertible_to<const S&, std::string_view>
    consteval FormatString(  // NOLINT(*-explicit-constructor)
        const S& str, const std::source_location loc = std::source_location::current()) noexcept
      : format(str), location(loc)
    {
    }
};

}  // namespace logger
