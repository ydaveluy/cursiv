// Two parents override a terminal with different converters, and the child's
// restatement names none: the terminal's value type is left undecided.
// expect LR: two parent grammars give this rule different converters
#pragma once

#include <cstdint>

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Counted : cursiv::Extends<cursiv::AstNode> {
  std::int64_t count = 0;
};
}  // namespace ast

struct Root : Base {
  struct NUM : terminal<some(d)> {};
  struct Top : rule<create<ast::Counted>(
                   kw<"count"> + assign<&ast::Counted::count>(ref<NUM>))> {};
  using entry = Top;
  using ignored = WS;
};

struct Left : cursiv::extends<Root> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::NUM>
  struct override<T> : terminal<as<std::int64_t>(super)> {};
};

struct Right : cursiv::extends<Root> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::NUM>
  struct override<T> : terminal<as<std::int32_t>(super)> {};
};

struct G : cursiv::extends<Left, Right> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::NUM>
  struct override<T> : terminal<some(d)> {};
};

}  // namespace refuse
