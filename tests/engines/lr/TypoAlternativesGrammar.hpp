#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace ttyporead {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  std::string_view value;
};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ABC : terminal<lit<"abc">> {};
  struct FIELD : terminal<lit<"field">> {};
  struct WordBang : terminal<lit<"word">> {};
  struct WordQuestion : terminal<lit<"word">> {};
  struct Root : rule<create<Doc>(
      (assign<&Doc::value>(ref<ABC>) + lit<";">) |
      (assign<&Doc::value>(ref<FIELD>) + kw<"tail">) |
      (lit<"["> + ((assign<&Doc::value>(ref<WordBang>) + lit<"!">) |
                    (assign<&Doc::value>(ref<WordQuestion>) + lit<"?">))))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace ttyporead
