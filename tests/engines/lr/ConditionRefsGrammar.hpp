#pragma once

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcondref {

namespace ast {
struct Num : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Num*> nums;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct D : terminal<some(cr<"0-9">)> {};
  struct W : terminal<some(cr<"0-9a-z">)> {};
  struct NoDot : rule<create<ast::Num> +
                      !(ref<D> + lit<".">)+assign<&ast::Num::text>(ref<D>)> {};
  struct Digits : rule<create<ast::Num> + &some(ref<D>) +
                       assign<&ast::Num::text>(ref<D>)> {};
  struct NoDigitStart : rule<create<ast::Num> + !repeat<1, 3>(ref<D>) +
                             assign<&ast::Num::text>(ref<W>)> {};
  struct Root : rule<create<ast::Doc>(many(
                    append<&ast::Doc::nums>((kw<"a"> + ref<NoDot>) |
                                            (kw<"b"> + ref<Digits>) |
                                            (kw<"c"> + ref<NoDigitStart>)) +
                    lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tcondref
