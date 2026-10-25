// How many nodes of a member's family one match of a shape leaves for the
// directive above it: what that directive's node path takes when the shape
// pays no value of its own.
#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

// The builder takes the first node of the family and ignores the others, so
// a node of another class is no node here. One that a rule of a base class
// builds may or may not belong: it counts in `hi` and not in `lo`.
template <class T, class L>
struct family_fit;
template <class T, class... Gs>
struct family_fit<T, TypeList<Gs...>> {
  static constexpr bool surely = (std::is_base_of_v<Gs, T> || ...);
  static constexpr bool maybe = surely || (std::is_base_of_v<T, Gs> || ...);
};

// A range capped at 2, and whether a repetition can hand over more than one.
consteval unsigned nodes_cap(std::size_t n) {
  return n > 2 ? 2u : static_cast<unsigned>(n);
}
consteval unsigned nodes_times(std::size_t a, unsigned b) {
  return a == 0 || b == 0 ? 0u : nodes_cap(a > 2 ? 2 : a * b);
}
template <class L, class E>
struct nodes_held {
  static constexpr unsigned lo = 0, hi = 0;
  static constexpr bool repeated = false;
};
template <class L, class T>
struct nodes_held_of {
  static constexpr unsigned lo = family_fit<T, L>::surely ? 1 : 0,
                            hi = family_fit<T, L>::maybe ? 1 : 0;
  static constexpr bool repeated = false;
};
template <class L, class R>
  requires(is_node_rule<named_rule_t<R>> || is_infix_rule<named_rule_t<R>>)
struct nodes_held<L, Ref<R>> : nodes_held_of<L, rule_type_t<named_rule_t<R>>> {
};
template <class L, class T, class E>
struct nodes_held<L, Create<T, E>> : nodes_held_of<L, T> {};
template <class L, class... Es>
struct nodes_held<L, Seq<Es...>> {
  static constexpr unsigned lo = nodes_cap((0u + ... + nodes_held<L, Es>::lo)),
                            hi = nodes_cap((0u + ... + nodes_held<L, Es>::hi));
  static constexpr bool repeated = (nodes_held<L, Es>::repeated || ...);
};
template <class L, class... Es>
struct nodes_held<L, Unordered<Es...>> : nodes_held<L, Seq<Es...>> {};
template <class L, class E0, class... Es>
struct nodes_held<L, Choice<E0, Es...>> {
  static constexpr unsigned lo =
      std::min({nodes_held<L, E0>::lo, nodes_held<L, Es>::lo...});
  static constexpr unsigned hi =
      std::max({nodes_held<L, E0>::hi, nodes_held<L, Es>::hi...});
  static constexpr bool repeated =
      nodes_held<L, E0>::repeated || (nodes_held<L, Es>::repeated || ...);
};
template <class L, std::size_t Mn, std::size_t Mx, class E>
struct nodes_held<L, Repeat<Mn, Mx, E>> {
  static constexpr unsigned lo = nodes_times(Mn, nodes_held<L, E>::lo),
                            hi = nodes_times(Mx, nodes_held<L, E>::hi);
  static constexpr bool repeated =
      nodes_held<L, E>::repeated || (Mx > 1 && nodes_held<L, E>::hi != 0);
};
template <class L, class E, str T>
struct nodes_held<L, Doc<E, T>> : nodes_held<L, E> {};
template <class L, class I, class H, class E>
struct nodes_held<L, Skip<I, H, E>> : nodes_held<L, E> {};
template <class L, class G, class E>
struct nodes_held<L, Embed<G, E>> : nodes_held<L, E> {};

// The absent arm of an at-most-once repetition writes nothing, so the item
// alone is counted.
template <class E>
struct once_item {
  using type = E;
};
template <std::size_t Mn, class E>
struct once_item<Repeat<Mn, 1, E>> : once_item<E> {};

}  // namespace cursiv::detail
