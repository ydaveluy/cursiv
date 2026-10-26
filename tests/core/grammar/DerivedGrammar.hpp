#pragma once

// A base and a dialect derived from it: the dialect overrides a terminal by
// name, adds a hidden one, and builds the base's tree. A refusal in a rule
// asks the terminal the dialect spells.

#include <charconv>
#include <concepts>
#include <string_view>
#include <system_error>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// A literal's guard names a terminal: the dialect's override is the one it
// asks, in both engines.
namespace tguard {

namespace ast {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};

}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct WORDCH : terminal<cr<"a-z">> {};
  struct IN : terminal<lit<"in", !ref<WORDCH>>> {};
  struct NUM : terminal<some(d)> {};
  struct DocRule : rule<create<ast::Doc>(
                       many(append<&ast::Doc::items>(ref<IN> | ref<NUM>)))> {};

  using entry = DocRule;
  using ignore = WS;
};

}  // namespace tguard


namespace tderive {

namespace ast {

struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  double value = 0;
};

struct Starred : cursiv::Extends<Item> {};

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct NUM : terminal<some(d), double> {};
  struct KW : terminal<kw<"let">> {};

  struct ItemRule : rule<!ref<KW> + assign<&ast::Item::name>(ref<ID>) +
                         lit<"="> + assign<&ast::Item::value>(ref<NUM>)> {};
  struct DocRule : rule<many(append<&ast::Doc::items>(ref<ItemRule>))> {};
  struct Entry
      : rule<ref<DocRule> |
             (lit<"guard:"> +
              skip(ignored<tguard::Base::WS>, hide<>,
                   ref<tguard::Base::DocRule> + eof))> {};

  using entry = Entry;
  using ignore = WS;
};

inline constexpr auto hex_or_dec = [](std::string_view t) {
  const bool hex = t.starts_with("0x");
  unsigned long long v = 0;
  const char* const last = t.data() + t.size();
  const auto [p, ec] = std::from_chars(t.data() + (hex ? 2 : 0), last, v,
                                       hex ? 16 : 10);
  if (ec != std::errc{} || p != last)
    return cursiv::ConversionResult<double>::err("number is out of range");
  return cursiv::ConversionResult<double>::ok(static_cast<double>(v));
};

struct Dialect : cursiv::extends<Base> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Base::NUM>
  struct override<T> : terminal<(lit<"0x"> + some(cr<"0-9a-f">)) | some(d),
                                cursiv::text_conv<hex_or_dec>> {};

  template <class T>
    requires std::same_as<T, Base::KW>
  struct override<T> : terminal<kw<"var">> {};

  template <class T>
    requires std::same_as<T, tguard::Base::WORDCH>
  struct override<T> : terminal<cr<"a-z0-9">> {};

  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};

  using hidden = list<COMMENT>;
};

// A dialect that adds an alternative to a node rule of its base: the new one
// builds a class derived from the rule's, and the base's own alternative stays.
struct RuleDialect : cursiv::extends<Base> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Base::ItemRule>
  struct override<T>
      : rule<create<ast::Starred>(
                 lit<"*"> + assign<&ast::Item::name>(ref<Base::ID>) + lit<"="> +
                 assign<&ast::Item::value>(ref<Base::NUM>)) |
             super> {};
  // This variant tests a node-rule override; the terminal guard variants
  // are already exercised by Base and Dialect.
  using entry = Base::DocRule;
};

}  // namespace tderive

// A dialect restating a terminal's text converter: a data rule joining its
// parts reads each through the dialect's converter.
namespace ttext {

namespace ast {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> paths;
};

}  // namespace ast

inline constexpr auto drop_one = [](std::string_view t) { return t.substr(1); };
inline constexpr auto drop_two = [](std::string_view t) { return t.substr(2); };

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct WORD
      : terminal<lit<"@"> + some(cr<"a-z@">), cursiv::text_conv<drop_one>> {};
  struct Path : rule<ref<WORD> + lit<"."> + ref<WORD>> {};
  struct DocRule
      : rule<create<ast::Doc>(many(append<&ast::Doc::paths>(ref<Path>)))> {};

  using entry = DocRule;
  using ignore = WS;
};

struct Dialect : cursiv::extends<Base> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Base::WORD>
  struct override<T>
      : terminal<lit<"@@"> + some(cr<"a-z">), cursiv::text_conv<drop_two>> {};
};

struct BaseLl : Base {};

struct DialectLl : cursiv::extends<BaseLl> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Base::WORD>
  struct override<T>
      : terminal<lit<"@@"> + some(cr<"a-z">), cursiv::text_conv<drop_two>> {};
};

}  // namespace ttext
