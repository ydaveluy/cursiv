// A guard that excludes a class of characters, where a match may end one byte
// into such a character: the rest of it is text past the token.
// expect LR: refused: terminal .NUM. may end a match before the end of .\\p{Lo}., which a guard excludes after the start of the terminal
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NUM : terminal<some(cr<"0-9">) + !uc::Lo + option(lit<"\xe4">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                     many(ref<NUM> | ref<ID>))> {};
  using entry = Root;
};

}  // namespace refuse
