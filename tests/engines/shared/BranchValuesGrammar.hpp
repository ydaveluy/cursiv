// One boolean three ways: a terminal whose alternatives each give a constant,
// the data rule that gives the same constants, and the same words in place. A
// value stands where a name can too, so the words compete with names as in a
// real grammar. Beside them, `as` converts the text or the parts of what it
// reads.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tbranch {

namespace ast {
enum class Size { none, small, large };
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  bool flag = false;
  string_view other;
  Size size = Size::none;
  int32 count = 0;
};
struct Items : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

inline constexpr auto count_parts = [](std::span<const std::string_view> p) {
  return static_cast<std::int32_t>(p.size());
};
inline constexpr auto text_length = [](std::string_view t) {
  return static_cast<std::int32_t>(t.size());
};

struct Suite : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  // One boolean, written as a terminal and as a data rule.
  struct BOOL : terminal<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)> {};
  struct Bool : rule<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)> {};
  // Two alternatives spell `s`: the first one written gives the value.
  struct SIZE : terminal<as<ast::Size::small>(kw<"s"> | kw<"small">) |
                         as<ast::Size::large>(ikw<"S"> | ikw<"large">)> {};

  struct TokenItem
      : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>) + lit<"="> +
                               (assign<&ast::Item::flag>(ref<BOOL>) |
                                assign<&ast::Item::other>(ref<ID>)) +
                               lit<";">)> {};
  struct RuleItem
      : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>) + lit<"="> +
                               (assign<&ast::Item::flag>(ref<Bool>) |
                                assign<&ast::Item::other>(ref<ID>)) +
                               lit<";">)> {};
  struct InlineItem
      : rule<create<ast::Item>(
            assign<&ast::Item::name>(ref<ID>) + lit<"="> +
            (assign<&ast::Item::flag>(as<true>(ikw<"TRUE">) |
                                      as<false>(ikw<"FALSE">)) |
             assign<&ast::Item::other>(ref<ID>)) +
            lit<";">)> {};
  // Only the words stand here, through the terminal, the data rule or in
  // place, so a misspelled one is repaired.
  struct FlagItem
      : rule<create<ast::Item>(
            assign<&ast::Item::name>(ref<ID>) +
            ((lit<"!"> + assign<&ast::Item::flag>(ref<Bool>)) |
             (lit<"~"> + assign<&ast::Item::flag>(ref<BOOL>)) |
             (lit<"*"> + assign<&ast::Item::flag>(as<true>(ikw<"TRUE">) |
                                                  as<false>(ikw<"FALSE">))) |
             (lit<"?"> + assign<&ast::Item::flag>(as<true>(ikw<"YES">) |
                                                  as<false>(ikw<"NO">)))) +
            lit<";">)> {};
  // The text or the parts, in place and through a data rule.
  struct NUM : terminal<some(d)> {};
  struct Measure : rule<as<text_length>(lit<"("> + ref<ID> + lit<")">) |
                        as<count_parts>(lit<"["> + many(ref<ID>) + lit<"]">)> {
  };
  struct CountItem
      : rule<create<ast::Item>(
            assign<&ast::Item::name>(ref<ID>) +
            ((lit<"#"> +
              assign<&ast::Item::count>(as<count_parts>(some(ref<ID>)) |
                                        as<std::int32_t>(ref<NUM>))) |
             (lit<"%"> + assign<&ast::Item::count>(ref<Measure>))) +
            lit<";">)> {};
  struct SizeItem : rule<create<ast::Item>(
                        assign<&ast::Item::name>(ref<ID>) + lit<":"> +
                        assign<&ast::Item::size>(ref<SIZE>) + lit<";">)> {};
  struct Root
      : rule<create<ast::Items>(
            (lit<"token:"> + many(append<&ast::Items::items>(ref<TokenItem>))) |
            (lit<"rule:"> + many(append<&ast::Items::items>(ref<RuleItem>))) |
            (lit<"inline:"> +
             many(append<&ast::Items::items>(ref<InlineItem>))) |
            (lit<"size:"> + many(append<&ast::Items::items>(ref<SizeItem>))) |
            (lit<"flag:"> + many(append<&ast::Items::items>(ref<FlagItem>))) |
            (lit<"count:"> +
             many(append<&ast::Items::items>(ref<CountItem>))))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tbranch
