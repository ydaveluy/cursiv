#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllstaged {

struct Statement : cursiv::Extends<cursiv::AstNode> {};
struct First : cursiv::Extends<Statement> { string_view text; };
struct Second : cursiv::Extends<Statement> { string_view text; };
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Statement*> values;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  // Distinct writes prevent PrefixFactor from replacing these choices.
  struct Unrelated : rule<
      create<First>(assign<&First::text>(lit<"x">) + lit<"a">) |
      create<Second>(assign<&Second::text>(lit<"x">) + lit<"b">)> {};
  struct AfterSplit : rule<
      create<First>(assign<&First::text>(lit<"x">) + lit<"a"> +
                    gap::line + lit<"z">) |
      create<Second>(assign<&Second::text>(lit<"x">) + lit<"b"> +
                     !gap::line + lit<"z">)> {};
  struct BeforeSplit : rule<
      create<First>(assign<&First::text>(lit<"x">) + gap::line + lit<"a">) |
      create<Second>(assign<&Second::text>(lit<"x">) + !gap::line + lit<"a">)> {};
  struct Head : rule<
      create<First>(gap::line + assign<&First::text>(lit<"x">) + lit<"a">) |
      create<Second>(!gap::line + assign<&Second::text>(lit<"x">) + lit<"b">)> {};
  struct Empty : rule<option(lit<"?">)> {};
  struct ReturnGuard : rule<create<First>(
      ref<Empty> + gap::line + assign<&First::text>(lit<"x">) + lit<"a">)> {};
  struct Root : rule<create<Document>(many(
      (lit<"u"> + append<&Document::values>(ref<Unrelated>) + lit<";">) |
      (lit<"s"> + append<&Document::values>(ref<AfterSplit>) + lit<";">) |
      (lit<"g"> + append<&Document::values>(ref<BeforeSplit>) + lit<";">) |
      (lit<"h"> + append<&Document::values>(ref<Head>) + lit<";">) |
      (lit<"n"> + append<&Document::values>(ref<ReturnGuard>) + lit<";">)) +
      eof)> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tllstaged
