// A `skip()` region whose skipper holds a terminal the caller reads as a token:
// the newline is skipped inside the brackets and a token again behind them.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllwiden {
struct Value : cursiv::Extends<cursiv::AstNode> {};
struct Scalar : cursiv::Extends<Value> {
  string_view text;
};
struct List : cursiv::Extends<Value> {
  vector<Value*> items;
};
struct Entry : cursiv::Extends<cursiv::AstNode> {
  string_view key;
  Value* value{};
};
struct File : cursiv::Extends<cursiv::AstNode> {
  vector<Entry*> entries;
};

struct Toml : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(option(lit<"\r">) + lit<"\n">)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};
  struct KEY : terminal<some(cr<"a-z">)> {};
  struct INT : terminal<some(d)> {};
  struct ListRule;
  struct ScalarRule : rule<create<Scalar>(assign<&Scalar::text>(ref<INT>))> {};
  static constexpr auto value = ref<ScalarRule> | ref<ListRule>;
  struct ListRule
      : rule<create<List>(
            skip(ignored<WS, NL>, hide<COMMENT>,
                 lit<"["> +
                     option(append<&List::items>(value) +
                            many(lit<","> + append<&List::items>(value)) +
                            option(lit<",">)) +
                     lit<"]">))> {};
  struct EntryRule
      : rule<create<Entry>(assign<&Entry::key>(ref<KEY>) + lit<"="> +
                           assign<&Entry::value>(value))> {};
  static constexpr auto trailing =
      tight(option(ref<COMMENT>));
  static constexpr auto line =
      option(append<&File::entries>(ref<EntryRule>)) + trailing;
  struct Root : rule<create<File>(line + many(ref<NL> + line))> {};
  using entry = Root;
  using ignore = WS;
};

struct Ll : Toml {};
}  // namespace tllwiden
