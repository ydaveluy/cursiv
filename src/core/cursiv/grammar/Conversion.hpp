#pragma once

#include <algorithm>
#include <cfenv>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/utils/TypeName.hpp"

// What a matched text becomes. A reader that only converts includes this
// file, which knows nothing of rules or the DSL.

namespace cursiv {

namespace detail {

// The AstNode vocabulary's numbers, int8 to float64, as a closed set. Plain
// `char` and the wide character types are left out: a grammar that wants one
// writes a converter.
template <class M>
inline constexpr bool from_chars_number =
    std::same_as<M, std::int8_t> || std::same_as<M, std::uint8_t> ||
    std::same_as<M, std::int16_t> || std::same_as<M, std::uint16_t> ||
    std::same_as<M, std::int32_t> || std::same_as<M, std::uint32_t> ||
    std::same_as<M, std::int64_t> || std::same_as<M, std::uint64_t> ||
    std::same_as<M, float> || std::same_as<M, double>;

// The target types the tree's own conversion reaches: the text, bool, the
// numbers above, and an enum by its enumerator names. A trait, since a
// `requires` on the call always answers yes.
template <class M>
inline constexpr bool has_default_convert =
    std::is_same_v<M, std::string_view> || std::is_same_v<M, std::string> ||
    std::is_same_v<M, bool> || from_chars_number<M> || std::is_enum_v<M>;

}  // namespace detail

// The customisation point, for a target of your own:
//   `static ConversionResult<T> convert(std::string_view)`
// Unspecialised, it is the named refusal in `rule_type`.
template <class T>
struct default_converter;

template <class T>
struct ConversionResult {
  std::optional<T> value;
  ParseDiagnostic error;
  constexpr explicit operator bool() const { return value.has_value(); }
  static constexpr ConversionResult ok(T v) { return {std::move(v), {}}; }
  // The words only: the diagnostic spans the whole record, so a parts
  // converter names the part that failed in its message.
  static constexpr ConversionResult err(std::string msg) {
    return {
        std::nullopt,
        {.kind = DiagnosticKind::conversion_error, .message = std::move(msg)}};
  }
};

namespace detail {

// At namespace scope, so it is not rebuilt on the stack at every call.
inline constexpr double kPow10[23] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// Clinger's exact fast path: a significand up to 2^53 times or over a power
// of ten up to 1e22 is one correctly rounded operation. Anything else, and
// every `float`, takes the library call.
[[gnu::always_inline]] constexpr bool fast_decimal(std::string_view t,
                                                   double& out) {
  const char* p = t.data();
  const char* const e = p + t.size();
  bool neg = false;
  if (p != e && *p == '-') {
    neg = true;
    ++p;
  }
  std::uint64_t sig = 0;
  int ndig = 0;
  while (p != e && static_cast<unsigned>(*p - '0') < 10u) {
    if (++ndig > 19) return false;
    sig = sig * 10 + static_cast<unsigned>(*p - '0');
    ++p;
  }
  int exp10 = 0;
  if (p != e && *p == '.') {
    ++p;
    while (p != e && static_cast<unsigned>(*p - '0') < 10u) {
      if (++ndig > 19) return false;
      sig = sig * 10 + static_cast<unsigned>(*p - '0');
      --exp10;
      ++p;
    }
  }
  if (ndig == 0) return false;
  if (p != e && (*p == 'e' || *p == 'E')) {
    ++p;
    bool eneg = false;
    if (p != e && (*p == '+' || *p == '-')) {
      eneg = (*p == '-');
      ++p;
    }
    int ev = 0;
    bool exponentDigits = false;
    while (p != e && static_cast<unsigned>(*p - '0') < 10u) {
      ev = ev * 10 + (*p - '0');
      exponentDigits = true;
      ++p;
      if (ev > 9999) return false;  // wider than the window either way
    }
    if (!exponentDigits) return false;
    exp10 += eneg ? -ev : ev;
  }
  if (p != e) return false;
  if (sig > (std::uint64_t{1} << 53)) return false;
  if (exp10 < -22 || exp10 > 22) return false;
  // The arithmetic must round like from_chars, independently of the host.
  if (exp10 != 0 && !std::is_constant_evaluated() &&
      std::fegetround() != FE_TONEAREST)
    return false;
  auto d = static_cast<double>(sig);
  d = exp10 >= 0 ? d * kPow10[exp10] : d / kPow10[-exp10];
  out = neg ? -d : d;
  return true;
}

// libc++ reads a decimal in the current rounding mode, where libstdc++ always
// rounds to nearest. Nothing to do where the host has only that mode.
#if defined(_LIBCPP_VERSION) && defined(FE_DOWNWARD)
template <class M>
constexpr std::from_chars_result from_chars(const char* first, const char* last,
                                            M& value) {
  if constexpr (std::floating_point<M>) {
    const int mode = std::fegetround();
    std::fesetround(FE_TONEAREST);
    const std::from_chars_result read = std::from_chars(first, last, value);
    std::fesetround(mode);
    return read;
  } else {
    return std::from_chars(first, last, value);
  }
}
#else
using std::from_chars;
#endif

}  // namespace detail

// The conversion of the closed set `has_default_convert` names, reached
// through `default_converter<T>`.
template <class M>
constexpr ConversionResult<M> default_convert(std::string_view text) {
  if constexpr (std::is_same_v<M, std::string_view>)
    return ConversionResult<M>::ok(text);
  else if constexpr (std::is_same_v<M, std::string>)
    return ConversionResult<M>::ok(std::string(text));
  else if constexpr (std::is_same_v<M, bool>) {
    const auto spells = [text](std::string_view expected) {
      return std::ranges::equal(text, expected, [](char t, char e) {
        return t == e || t == e - 'a' + 'A';
      });
    };
    return spells("true") ? ConversionResult<M>::ok(true)
           : spells("false")
               ? ConversionResult<M>::ok(false)
               : ConversionResult<M>::err("expected 'true' or 'false'");
  } else if constexpr (detail::from_chars_number<M>) {
    M v{};
    // `std::from_chars` refuses a leading '+', so one is dropped: "+5" reads 5,
    // "++5" still fails.
    std::string_view digits = text;
    if (!digits.empty() && digits.front() == '+') {
      digits.remove_prefix(1);
      if (!digits.empty() && digits.front() == '-')
        return ConversionResult<M>::err("not a number");
    }
    if constexpr (std::same_as<M, double>) {
      double fast = 0.0;
      if (detail::fast_decimal(digits, fast))
        return ConversionResult<M>::ok(fast);
    }
    const auto [p, ec] =
        detail::from_chars(digits.data(), digits.data() + digits.size(), v);
    if (ec == std::errc{} && p == digits.data() + digits.size())
      return ConversionResult<M>::ok(v);
    return ConversionResult<M>::err(ec == std::errc::result_out_of_range
                                        ? "value out of range"
                                        : "not a number");
  }
  // An enum reads its enumerator names: `visibility=priv` writes `Vis::priv`
  // with no converter.
  else if constexpr (std::is_enum_v<M>) {
    if (M found{};
        detail::find_enumerator<M>(text, found, detail::enum_probes{}))
      return ConversionResult<M>::ok(found);
    return ConversionResult<M>::err("not an enumerator of " +
                                    std::string(detail::type_name<M>()));
  } else
    static_assert(detail::has_default_convert<M>,
                  "no default converter for this member type: specialise "
                  "cursiv::default_converter<T>, or convert with as<f>(e)");
}

template <class T>
  requires detail::has_default_convert<T>
struct default_converter<T> {
  static constexpr ConversionResult<T> convert(std::string_view text) {
    return default_convert<T>(text);
  }
};

}  // namespace cursiv
