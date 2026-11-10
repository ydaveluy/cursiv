#pragma once

#include <type_traits>

#include "cursiv/utils/typelist.hpp"

// What kind of rule this is: the three tags a rule declares, the predicate
// read from a tag alone, and the refusal of an escaped `super`.

namespace cursiv {
namespace detail {

struct terminal_kind {};
struct node_kind {};
struct infix_kind {};

// `plug` splices every `super` away before any walk runs, so one that reaches
// a walk was spelled outside an override body. Every total walk ends up here.
template <class E>
struct super_escaped {
  static_assert(dependent_false<E>,
                "`super` stands for the body of the overridden rule: use it "
                "only inside the body of an override");
  using type = E;
  static constexpr bool value = false;
};

}  // namespace detail

template <class R>
inline constexpr bool is_terminal_rule =
    std::is_same_v<typename R::kind, detail::terminal_kind>;

}  // namespace cursiv
