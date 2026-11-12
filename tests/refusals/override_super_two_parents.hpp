// Two parents both override a rule and the child restates it with `super`:
// the restatement chooses between them, so `super` names neither.
// expect LR at compile: so .super. in the override here names neither
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Root : Base {
  struct Item : rule<create<ast::Node>(kw<"item"> +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Item;
  using ignored = WS;
};

struct Left : cursiv::extends<Root> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::Item>
  struct override<T>
      : rule<create<ast::Node>(kw<"left"> + assign<&ast::Node::name>(ref<ID>)) |
             super> {};
};

struct Right : cursiv::extends<Root> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::Item>
  struct override<T>
      : rule<create<ast::Node>(kw<"right"> +
                               assign<&ast::Node::name>(ref<ID>)) |
             super> {};
};

struct G : cursiv::extends<Left, Right> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Root::Item>
  struct override<T>
      : rule<create<ast::Node>(kw<"both"> + assign<&ast::Node::name>(ref<ID>)) |
             super> {};
};

}  // namespace refuse
