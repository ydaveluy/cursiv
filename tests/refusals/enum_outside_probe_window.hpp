// An enumerator whose value lies outside the probe window has no name the
// conversion can read: the refusal says where the window is.
#pragma once

#include <cstdint>

#include "refusals/head.hpp"

namespace refuse {

enum class Flags : std::uint16_t { none = 0, big = 1024 };

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Flags flags = Flags::none;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Holder>(assign<&ast::Holder::flags>(kw<"big">))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
