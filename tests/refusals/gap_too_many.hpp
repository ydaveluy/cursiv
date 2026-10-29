// Thirty-three implied tokens: a reading holds a bit for thirty-two.
// expect LR: has more than 255 gap conditions, or more than 32 implied tokens that can be inserted at one position
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(
            assign<&ast::Node::name>(ref<ID>) +
            implied(lit<"a">, gap::line) +
            implied(lit<"b">, gap::line) +
            implied(lit<"c">, gap::line) +
            implied(lit<"d">, gap::line) +
            implied(lit<"e">, gap::line) +
            implied(lit<"f">, gap::line) +
            implied(lit<"g">, gap::line) +
            implied(lit<"h">, gap::line) +
            implied(lit<"i">, gap::line) +
            implied(lit<"j">, gap::line) +
            implied(lit<"k">, gap::line) +
            implied(lit<"l">, gap::line) +
            implied(lit<"m">, gap::line) +
            implied(lit<"n">, gap::line) +
            implied(lit<"o">, gap::line) +
            implied(lit<"p">, gap::line) +
            implied(lit<"q">, gap::line) +
            implied(lit<"r">, gap::line) +
            implied(lit<"s">, gap::line) +
            implied(lit<"t">, gap::line) +
            implied(lit<"u">, gap::line) +
            implied(lit<"v">, gap::line) +
            implied(lit<"w">, gap::line) +
            implied(lit<"x">, gap::line) +
            implied(lit<"y">, gap::line) +
            implied(lit<"z">, gap::line) +
            implied(lit<"aa">, gap::line) +
            implied(lit<"ab">, gap::line) +
            implied(lit<"ac">, gap::line) +
            implied(lit<"ad">, gap::line) +
            implied(lit<"ae">, gap::line) +
            implied(lit<"af">, gap::line) +
            implied(lit<"ag">, gap::line))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
