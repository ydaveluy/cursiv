// A precedence table of more levels than a byte numbers.
// expect LR LL: refused .tooManyPrecedences..*rule .Sum. declares more than 255 precedence levels
#pragma once

#include <cstddef>
#include <utility>

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Bin : cursiv::Extends<cursiv::AstNode> {
  Bin* left = nullptr;
  string_view op;
  Bin* right = nullptr;
};
}  // namespace ast

template <std::size_t... Is>
constexpr auto plus_levels(std::index_sequence<Is...>) {
  return cursiv::levels(((void)Is, cursiv::left(cursiv::lit<"+">))...);
}

struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum
      : infix<Primary, ast::Bin, plus_levels(std::make_index_sequence<256>{})> {
  };
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
