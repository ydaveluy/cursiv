#pragma once

#include <cfenv>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/utils/TypeName.hpp"

// What a matched text becomes. Declaring a converter is part of writing a
// grammar, so `GrammarSpec.hpp` includes this and an author needs one header
// -- but a reader that only converts takes this one, which knows nothing of
// rules, combinators or the DSL.

namespace cursiv {

// Exactly the AstNode vocabulary's numbers, int8/uint8 .. float64, spelled
// out rather than derived from `is_arithmetic_v`: int8/uint8 are
// signed/unsigned char, so this is a closed set, not "every integer minus
// the character types". Plain `char` is off it because routing "7" through
// its from_chars overload yields 0x07, not '7'; wchar_t/char8_t/char16_t/
// char32_t are off it because they have no overload at all, which would be
// a raw compiler error rather than a diagnostic. A grammar wanting one of
// those gets the named "no conversion" refusal and writes a converter.
template <class M>
inline constexpr bool from_chars_number =
    std::same_as<M, std::int8_t> || std::same_as<M, std::uint8_t> ||
    std::same_as<M, std::int16_t> || std::same_as<M, std::uint16_t> ||
    std::same_as<M, std::int32_t> || std::same_as<M, std::uint32_t> ||
    std::same_as<M, std::int64_t> || std::same_as<M, std::uint64_t> ||
    std::same_as<M, float> || std::same_as<M, double>;

// Which target types the tree's own conversion reaches: the identity, the
// boolean, the arithmetic set, and an enum matched against its enumerator
// names. A predicate, because the validation must ask without instantiating
// the conversion: `requires { default_convert<M>(t); }` answers yes for
// every M -- the call expression is always well formed and the refusal is a
// static_assert inside the body -- so a trait is the only honest test. Text
// is `string_view` and nothing else -- the tree borrows the document, so no
// member owns its bytes and no default conversion materialises any.
template <class M>
inline constexpr bool has_default_convert =
    std::is_same_v<M, std::string_view> || std::is_same_v<M, bool> ||
    from_chars_number<M> || std::is_enum_v<M>;

// The customisation point, in two forms and never both -- one converter per
// rule:
//   `using result = T;`  announces a target the tree's own `default_convert`
//                        already reaches, which is the closed set above;
//   `static ConversionResult<T> convert(std::string_view)`  is the
//                        conversion, for a target of your own, and takes the
//                        optional `std::pmr::memory_resource*` second
//                        parameter exactly as a converter function does.
// A specialisation announcing a `result` `default_convert` does not reach is
// the named refusal on the member that reads it. Unspecialised it is the
// named refusal in `rule_type`.
//
// No `std::string` here, deliberately: an owning string materialises, and
// the text law leaves no member to hold one. A rule that has to build its
// answer takes the arena through a converter function that names the
// resource.
template <class T>
struct default_converter;
template <class T>
  requires has_default_convert<T>
struct default_converter<T> {
  using result = T;
};

template <class T>
struct ConversionResult {
  std::optional<T> value;
  ParseDiagnostic error;
  constexpr explicit operator bool() const { return value.has_value(); }
  static constexpr ConversionResult ok(T v) { return {std::move(v), {}}; }
  // The words only: the builder ranges the diagnostic over the record it was
  // reading, which for a parts converter is the whole rule and never the part
  // that failed. A converter that has to say which part says it in the
  // message.
  static constexpr ConversionResult err(std::string msg) {
    return {
        std::nullopt,
        {.kind = DiagnosticKind::conversion_error, .message = std::move(msg)}};
  }
};

namespace detail {

// Namespace scope, not function scope. As a local `constexpr double[23]` the
// compiler materialises the 184-byte table on the stack of every call, which
// costs more than the conversion it serves. At namespace scope it is one
// .rodata object indexed in place.
inline constexpr double kPow10[23] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// Clinger's exact fast path (Clinger 1990), in front of the library call.
// `std::from_chars(..., double&)` is an out-of-line call into libstdc++ --
// an undefined symbol in every binary that links cursiv -- and on a short
// number this path saves more than half of its instructions.
//
// Why the answer is bit-identical, in one sentence: when the decimal
// significand is at most 2^53 it is an exact double, when |exp10| <= 22 the
// power of ten is an exact double, so `sig * 10^e` (or `sig / 10^-e`) is one
// IEEE operation on two exact operands -- and a single IEEE operation is
// correctly rounded by definition, which is exactly what from_chars promises.
//
// Everything else answers false and the library call runs unchanged: more
// than 19 significant digits (where the uint64 accumulator would wrap), a
// wider exponent, inf/nan, a hex literal, or any residue after the number.
// The 19 bound is what makes the accumulator sound: 9'999'999'999'999'999'999
// < 2^64-1, so `sig` is exact when the `> 2^53` test is applied to it.
//
// Double only, deliberately: for `float` the same table would round decimal
// -> double -> float and double rounding can differ from the single
// correctly rounded result. `float` keeps the library call.
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
  double d = static_cast<double>(sig);
  d = exp10 >= 0 ? d * kPow10[exp10] : d / kPow10[-exp10];
  out = neg ? -d : d;
  return true;
}

}  // namespace detail

template <class M>
constexpr ConversionResult<M> default_convert(std::string_view text) {
  if constexpr (std::is_same_v<M, std::string_view>)
    return ConversionResult<M>::ok(text);
  else if constexpr (std::is_same_v<M, bool>) {
    const auto spells = [text](std::string_view expected) {
      if (text.size() != expected.size()) return false;
      for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] != expected[i] && text[i] != expected[i] - 'a' + 'A')
          return false;
      return true;
    };
    return spells("true") ? ConversionResult<M>::ok(true)
           : spells("false")
               ? ConversionResult<M>::ok(false)
               : ConversionResult<M>::err("expected 'true' or 'false'");
  } else if constexpr (from_chars_number<M>) {
    M v{};
    // std::from_chars refuses a leading '+' for every arithmetic type (only
    // '-' is a sign it reads), so an optional-sign grammar like
    // `opt(cr<"+-">) + some(d)` would turn the legal "+5" into "not a
    // number". Strip at most one leading '+': "+5" parses as 5, "++5" still
    // fails (the surviving '+' is not a digit either), "-5" is untouched.
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
        std::from_chars(digits.data(), digits.data() + digits.size(), v);
    if (ec == std::errc{} && p == digits.data() + digits.size())
      return ConversionResult<M>::ok(v);
    return ConversionResult<M>::err(ec == std::errc::result_out_of_range
                                        ? "value out of range"
                                        : "not a number");
  }
  // The enumerator names are the language: `visibility=priv` writes
  // `Vis::priv` with no converter and no table to keep in sync. The
  // enumerator list is a static array, so the loop is unrolled and each
  // comparison is against a literal.
  else if constexpr (std::is_enum_v<M>) {
    M found{};
    if (detail::find_enumerator<M>(text, found, detail::enum_probes{}))
      return ConversionResult<M>::ok(found);
    return ConversionResult<M>::err("not an enumerator of " +
                                    std::string(detail::type_name<M>()));
  } else
    static_assert(has_default_convert<M>,
                  "no default converter for this member type");
}

// The text law says a member holds a view. A converter whose value the
// source does not literally spell -- a decoded literal, a case-folded name
// -- allocates its bytes in the resource the builder hands it and answers a
// view of them. That resource is the pool the CST and the AST share: its
// deallocate is a no-op and its buffer is stable, so the view lives exactly
// as long as the `ParseResult`.
//
// A view, not a string, and these two helpers exist so no converter has to
// spell the cast and the alignment: an `std::pmr::string` would keep a short
// result in its small-string buffer, on the converter's own stack frame
// rather than in `mem`, and the member would view a dead local.

// The bytes are already assembled: copy them across and answer the copy.
inline std::string_view copy_into(std::pmr::memory_resource* mem,
                                  std::string_view text) {
  // A null data pointer means an unwritten AST member, even at length zero.
  char* const buf = static_cast<char*>(
      mem->allocate(text.empty() ? 1 : text.size(), alignof(char)));
  if (!text.empty())
    std::char_traits<char>::copy(buf, text.data(), text.size());
  return std::string_view(buf, text.size());
}

// The bytes have still to be written: `n` upper-bounds them, the caller fills
// what it needs and answers a view of the prefix it actually wrote. One pass,
// no intermediate string.
inline std::span<char> text_room(std::pmr::memory_resource* mem,
                                 std::size_t n) {
  return std::span<char>(
      static_cast<char*>(mem->allocate(n == 0 ? 1 : n, alignof(char))), n);
}

}  // namespace cursiv
