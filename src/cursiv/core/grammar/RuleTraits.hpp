#pragma once

#include <type_traits>

#include "cursiv/core/utils/typelist.hpp"

// What kind of rule this is, and nothing that depends on the answer: the
// three tags a rule declares, the one predicate decidable from a tag alone,
// and the walk-time refusal of an escaped `super`. Separate from
// GrammarSpec, which a reader asking only this would otherwise pull in whole.

namespace cursiv {
namespace detail {

struct terminal_kind {};
struct node_kind {};
struct infix_kind {};

// `plug` splices every `super` away before any walk runs, so one that reaches
// a walk was spelled outside an override body. Every total walk lands here.
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
