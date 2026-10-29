#pragma once

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace treserved {

namespace ast {
struct Name : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Name*> names;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  static constexpr auto Reserved =
      kw<"alpha"> | kw<"beta"> | kw<"gamma"> | kw<"delta">;
  struct NameRule
      : rule<create<ast::Name> + !Reserved + assign<&ast::Name::text>(ref<ID>)> {
  };
  struct DocRule
      : rule<create<ast::Doc>(
            many(append<&ast::Doc::names>(ref<NameRule>) + lit<";">))> {};
  using entry = DocRule;
  using ignored = WS;
};

}  // namespace treserved
