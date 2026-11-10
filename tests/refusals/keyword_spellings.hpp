// A case-insensitive keyword of twelve letters whose two cases start with
// different bytes, 4 096 spellings, and a hidden comment that starts with it
// and reads on: the lexer handles at most 1 024 such spellings.
// expect LR: refused: terminal .* has more than 1 024 keyword spellings, each word counted once for each case of its letters, and some trivia reads on past one of them
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  static constexpr auto word_boundary = uc::XID_Continue;
  struct REMARK : terminal<ikw<"ΥΠΕΡΣΥΝΤΡΙΠΤΙΚΟΤΑΤΟΣ"> + many(cr<R"(^\r\n)">)> {
  };
  struct Root : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ikw<"ΥΠΕΡΣΥΝΤΡΙΠΤΙΚΟΤΑΤΟΣ">))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = REMARK;
};

}  // namespace refuse
