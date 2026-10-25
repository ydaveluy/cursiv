#pragma once

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cursiv::type_name_detail {
// A separate scope keeps GCC from abbreviating nested cursiv::detail names.
template <class T>
constexpr std::string_view signature() {
#if defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#else
  return "?";
#endif
}
}  // namespace cursiv::type_name_detail

// How a thing is named when it has no identifier of its own: reflection names
// whatever has an identifier to reflect on, `__PRETTY_FUNCTION__` names what
// has none -- split to its last component, and the one compiler-specific
// spelling a committed file carries.

namespace cursiv::detail {

// Compiler-rendered C++ literals may contain delimiters that belong to the
// value, not the surrounding type. Returns `at` outside a quoted literal.
constexpr std::size_t cpp_literal_end(std::string_view s, std::size_t at) {
  if (at >= s.size() || (s[at] != '\'' && s[at] != '"')) return at;
  const char quote = s[at++];
  while (at < s.size()) {
    const char ch = s[at++];
    if (ch == quote) break;
    if (ch == '\\' && at < s.size()) ++at;
  }
  return at;
}

// None has an identifier to reflect on and the two compilers disagree on the
// pretty form, so the portable spelling is written out. Anything not listed
// answers empty and falls through to the pretty split.
template <class T>
consteval std::string_view fundamental_name() {
  using U = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<U, void>)
    return "void";
  else if constexpr (std::is_same_v<U, bool>)
    return "bool";
  else if constexpr (std::is_same_v<U, char>)
    return "char";
  else if constexpr (std::is_same_v<U, signed char>)
    return "signed char";
  else if constexpr (std::is_same_v<U, unsigned char>)
    return "unsigned char";
  else if constexpr (std::is_same_v<U, char8_t>)
    return "char8_t";
  else if constexpr (std::is_same_v<U, char16_t>)
    return "char16_t";
  else if constexpr (std::is_same_v<U, char32_t>)
    return "char32_t";
  else if constexpr (std::is_same_v<U, wchar_t>)
    return "wchar_t";
  else if constexpr (std::is_same_v<U, short>)
    return "short";
  else if constexpr (std::is_same_v<U, unsigned short>)
    return "unsigned short";
  else if constexpr (std::is_same_v<U, int>)
    return "int";
  else if constexpr (std::is_same_v<U, unsigned int>)
    return "unsigned int";
  else if constexpr (std::is_same_v<U, long>)
    return "long";
  else if constexpr (std::is_same_v<U, unsigned long>)
    return "unsigned long";
  else if constexpr (std::is_same_v<U, long long>)
    return "long long";
  else if constexpr (std::is_same_v<U, unsigned long long>)
    return "unsigned long long";
  else if constexpr (std::is_same_v<U, float>)
    return "float";
  else if constexpr (std::is_same_v<U, double>)
    return "double";
  else if constexpr (std::is_same_v<U, long double>)
    return "long double";
  else if constexpr (std::is_same_v<U, std::nullptr_t>)
    return "nullptr_t";
  else
    return {};
}

// What follows `prefix` in the compiler's rendering, up to the `;` before the
// next template parameter or, when there is none, the closing `]`. One function
// for both compilers and for both a type and a member: only the prefix differs.
constexpr std::string_view after(std::string_view pretty,
                                 std::string_view prefix) {
  // Manual loops on purpose: g++ at -O3 constant-folds `string_view::find` over
  // `__PRETTY_FUNCTION__` to npos even when the character is present.
  std::size_t at = std::string_view::npos;
  for (std::size_t i = 0; i + prefix.size() <= pretty.size(); ++i) {
    std::size_t j = 0;
    while (j < prefix.size() && pretty[i + j] == prefix[j]) ++j;
    if (j == prefix.size()) {
      at = i;
      break;
    }
  }
  if (at == std::string_view::npos) return pretty;
  const std::string_view rest = pretty.substr(at + prefix.size());
  std::size_t end = rest.size();
  for (std::size_t i = 0; i < rest.size(); ++i) {
    const auto quoted = cpp_literal_end(rest, i);
    if (quoted != i) {
      i = quoted - 1;
      continue;
    }
    if (rest[i] == ';') {
      end = i;
      break;
    }
  }
  if (end == rest.size() && end > 0) end = rest.size() - 1;  // the closing ']'
  return rest.substr(0, end);
}

template <class T>
constexpr std::string_view raw_type_name() {
#if defined(__clang__)
  return after(type_name_detail::signature<T>(), "[T = ");
#elif defined(__GNUC__)
  return after(type_name_detail::signature<T>(), "[with T = ");
#else
  return "?";
#endif
}

template <auto P>
constexpr std::string_view raw_member_name() {
#if defined(__clang__)
  return after(__PRETTY_FUNCTION__, "[P = ");
#elif defined(__GNUC__)
  return after(__PRETTY_FUNCTION__, "[with auto P = ");
#else
  return "?";
#endif
}

template <auto E>
constexpr std::string_view raw_enum_name() {
#if defined(__clang__)
  return after(__PRETTY_FUNCTION__, "[E = ");
#elif defined(__GNUC__)
  return after(__PRETTY_FUNCTION__, "[with auto E = ");
#else
  return "?";
#endif
}

constexpr bool is_identifier(std::string_view s) {
  if (s.empty()) return false;
  if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') ||
        s[0] == '_'))
    return false;
  for (const char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_'))
      return false;
  return true;
}

// The window of values an enumerator may have, probed one instantiation each:
// negatives (a sentinel), and enough positives for the single-bit spellings
// of a flag set. Outside it an enumerator has no spellable name, and where
// two share a value only the first is reachable -- the probe answers a
// value, not a declaration.
#ifndef CURSIV_ENUM_PROBE_MIN
#define CURSIV_ENUM_PROBE_MIN (-64)
#endif
#ifndef CURSIV_ENUM_PROBE_MAX
#define CURSIV_ENUM_PROBE_MAX 512
#endif
inline constexpr int kEnumProbeMin = CURSIV_ENUM_PROBE_MIN;
inline constexpr int kEnumProbeMax = CURSIV_ENUM_PROBE_MAX;
static_assert(kEnumProbeMin <= 0 && kEnumProbeMax > 0,
              "the enum probe window spans zero");
using enum_probes =
    std::make_integer_sequence<int, kEnumProbeMax - kEnumProbeMin>;

// The last `::`-separated component, ignoring anything inside <> so a template
// argument's own qualification cannot confuse the split.
constexpr std::string_view last_component(std::string_view full) {
  int depth = 0;
  std::size_t last = 0;
  for (std::size_t i = 0; i + 1 < full.size(); ++i) {
    const auto quoted = cpp_literal_end(full, i);
    if (quoted != i) {
      i = quoted - 1;
      continue;
    }
    if (full[i] == '<')
      ++depth;
    else if (full[i] == '>')
      --depth;
    else if (depth == 0 && full[i] == ':' && full[i + 1] == ':')
      last = i + 2;
  }
  return full.substr(last);
}

// The identifier alone. A specialisation answers its template's name, the two
// compilers spelling an argument list differently, and g++ can append an
// `@`-suffixed attachment that is not part of the name.
constexpr std::string_view without_decorations(std::string_view name) {
  for (std::size_t i = 0; i < name.size(); ++i)
    if (name[i] == '<' || name[i] == '@') return name.substr(0, i);
  return name;
}

// An enumerator renders as its name, a value without one as a cast --
// `(Mode)7`, or `((anonymous namespace)::Mode)7`. Only a plain identifier is a
// name.
template <auto E>
constexpr bool is_enumerator() {
  return is_identifier(without_decorations(last_component(raw_enum_name<E>())));
}

// The identifier copied into storage of its own length: a view into the
// pretty string would keep the whole of it alive in the binary.
template <const std::string_view& V>
struct name_store {
  static constexpr auto chars = [] {
    std::array<char, V.size()> a{};
    for (std::size_t i = 0; i < V.size(); ++i) a[i] = V[i];
    return a;
  }();
  static constexpr std::string_view view{chars.data(), chars.size()};
};

template <class T>
consteval std::string_view type_name_view() {
  if constexpr (!fundamental_name<T>().empty())
    return fundamental_name<T>();
  else
    return without_decorations(last_component(raw_type_name<T>()));
}
template <class T>
inline constexpr std::string_view kTypeNameView = type_name_view<T>();

// The one accessor: a fundamental type is spelled by hand, the compilers
// agreeing on the rest.
template <class T>
constexpr std::string_view type_name() {
  return name_store<kTypeNameView<T>>::view;
}

// The name of one probed value, empty when it is not an enumerator.
template <auto E>
constexpr std::string_view enumerator_name() {
  if constexpr (is_enumerator<E>())
    return without_decorations(last_component(raw_enum_name<E>()));
  else
    return {};
}

// The enumerator whose name is `text`, or nothing. Probing one value per index
// keeps every comparison against a literal.
template <class M, int... I>
constexpr bool find_enumerator(std::string_view text, M& out,
                               std::integer_sequence<int, I...>) {
  constexpr auto at = [](int i) { return static_cast<M>(i + kEnumProbeMin); };
  return (... ||
          (!enumerator_name<at(I)>().empty() && enumerator_name<at(I)>() == text
               ? (out = at(I), true)
               : false));
}
}  // namespace cursiv::detail
