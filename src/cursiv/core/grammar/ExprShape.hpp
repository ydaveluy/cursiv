// The grammar vocabulary as a reader sees it: the children a structural walk
// descends into, and how to rebuild a node around new ones. Spelled once here
// rather than in `expr.hpp`, which declares the language and reads nothing.
#pragma once

#include <cstddef>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

// No permissive default: a node missing here would answer "no children", and
// a rule named only under it would leave the grammar in silence.
template <class E>
struct ExprShape {
  static_assert(dependent_false<E>,
                "this combinator is not in the grammar vocabulary: give it an "
                "ExprShape<> (children + rebind) before any walk can see it");
};

// An atom, and the two operands that are not expressions: a literal's guard
// and a rule's name are read by the walks that care, never descended.
template <class E>
struct ExprAtom {
  using kids = TypeList<>;
  template <class...>
  using rebind = E;
};
template <>
struct ExprShape<Epsilon> : ExprAtom<Epsilon> {};
template <>
struct ExprShape<Byte> : ExprAtom<Byte> {};
template <>
struct ExprShape<Eof> : ExprAtom<Eof> {};
template <>
struct ExprShape<Indent> : ExprAtom<Indent> {};
template <>
struct ExprShape<Dedent> : ExprAtom<Dedent> {};
template <class E>
struct ExprShape<GapHas<E>> : ExprAtom<GapHas<E>> {};
template <class E>
struct ExprShape<GapIs<E>> : ExprAtom<GapIs<E>> {};
template <class T, class C>
struct ExprShape<Implied<T, C>> : ExprAtom<Implied<T, C>> {};
template <std::uint64_t M, bool N, str S>
struct ExprShape<CpSet<M, N, S>> : ExprAtom<CpSet<M, N, S>> {};
template <str S, bool CI, class Gd>
struct ExprShape<Lit<S, CI, Gd>> : ExprAtom<Lit<S, CI, Gd>> {};
// One kid and nothing else: the prose is not a shape, so a splice rebuilds
// the body and carries the prose across unchanged.
template <class E, str T>
struct ExprShape<Doc<E, T>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Doc<K..., T>;
};
template <class R>
struct ExprShape<Ref<R>> : ExprAtom<Ref<R>> {};
template <class C, class E>
struct ExprShape<As<C, E>> {
  using kids = TypeList<E>;
  template <class K>
  using rebind = As<C, K>;
};
template <class D>
struct ExprShape<SuperT<D>> : ExprAtom<SuperT<D>> {};

template <class... Es>
struct ExprShape<Seq<Es...>> {
  using kids = TypeList<Es...>;
  template <class... K>
  using rebind = Seq<K...>;
};
template <class... Es>
struct ExprShape<Choice<Es...>> {
  using kids = TypeList<Es...>;
  template <class... K>
  using rebind = Choice<K...>;
};
template <class... Es>
struct ExprShape<Unordered<Es...>> {
  using kids = TypeList<Es...>;
  template <class... K>
  using rebind = Unordered<K...>;
};
template <class O, class Cl>
struct ExprShape<Through<O, Cl>> {
  using kids = TypeList<O, Cl>;
  template <class... K>
  using rebind = Through<K...>;
};
template <class G, class E>
struct ExprShape<Embed<G, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Embed<G, K...>;
};
template <class Tag, class E>
struct ExprShape<Mark<Tag, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Mark<Tag, K...>;
};
template <class Tag>
struct ExprShape<Same<Tag>> : ExprAtom<Same<Tag>> {};
template <std::size_t Mn, std::size_t Mx, class E>
struct ExprShape<Repeat<Mn, Mx, E>> {
  using kids = TypeList<E>;
  template <class K>
  using rebind = Repeat<Mn, Mx, K>;
};
template <class P, class B>
struct ExprShape<Commit<P, B>> {
  using kids = TypeList<P, B>;
  template <class... K>
  using rebind = Commit<K...>;
};
template <class E>
struct ExprShape<Trigger<E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Trigger<K...>;
};
template <class E>
struct ExprShape<Not<E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Not<K...>;
};
template <class E>
struct ExprShape<And<E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = And<K...>;
};
template <auto Sp, class E>
struct ExprShape<Assign<Sp, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Assign<Sp, K...>;
};
template <auto Sp, class E>
struct ExprShape<Append<Sp, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Append<Sp, K...>;
};
template <auto P, class E>
struct ExprShape<Flag<P, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Flag<P, K...>;
};
template <class T, class E>
struct ExprShape<Create<T, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Create<T, K...>;
};
template <class T, auto P, class E>
struct ExprShape<Nest<T, P, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Nest<T, P, K...>;
};
// The two lists are a configuration, not children: they name rules, and the
// walk that reaches rules says so itself.
template <class I, class H, class E>
struct ExprShape<Skip<I, H, E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = Skip<I, H, K...>;
};

// A node whose reading is its one child's: prose, a mark, a region or a
// directive adds to what it wraps and takes nothing from it. A reader that
// reads one of these differently says so in a specialization of its own.
template <class E>
inline constexpr bool transparent = false;
template <class E, str T>
inline constexpr bool transparent<Doc<E, T>> = true;
template <class Tag, class E>
inline constexpr bool transparent<Mark<Tag, E>> = true;
template <class I, class H, class E>
inline constexpr bool transparent<Skip<I, H, E>> = true;
template <class G, class E>
inline constexpr bool transparent<Embed<G, E>> = true;
template <auto Sp, class E>
inline constexpr bool transparent<Assign<Sp, E>> = true;
template <auto Sp, class E>
inline constexpr bool transparent<Append<Sp, E>> = true;
template <auto P, class E>
inline constexpr bool transparent<Flag<P, E>> = true;
template <class E>
using through_t = tl_at_t<0, typename ExprShape<E>::kids>;

// A node that only gathers its kids: a reader that folds over one reads each
// kid the way it reads the node.
template <class E>
inline constexpr bool grouping = false;
template <class... Es>
inline constexpr bool grouping<Seq<Es...>> = true;
template <class... Es>
inline constexpr bool grouping<Choice<Es...>> = true;
template <class... Es>
inline constexpr bool grouping<Unordered<Es...>> = true;
template <class P, class B>
inline constexpr bool grouping<Commit<P, B>> = true;
template <std::size_t Mn, std::size_t Mx, class E>
inline constexpr bool grouping<Repeat<Mn, Mx, E>> = true;
template <class I, class H, class E>
inline constexpr bool grouping<Skip<I, H, E>> = true;
template <class G, class E>
inline constexpr bool grouping<Embed<G, E>> = true;

// Whether an expression names a directive of its own, one nested operand not
// counted: it is the rule of its own already.
template <class E>
inline constexpr bool is_directive = false;
template <class T, class E>
inline constexpr bool is_directive<Create<T, E>> = true;
template <class T, auto P, class E>
inline constexpr bool is_directive<Nest<T, P, E>> = true;
template <auto Sp, class E>
inline constexpr bool is_directive<Assign<Sp, E>> = true;
template <auto Sp, class E>
inline constexpr bool is_directive<Append<Sp, E>> = true;
template <auto P, class E>
inline constexpr bool is_directive<Flag<P, E>> = true;

template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool holds_directive = is_directive<E>;
template <class E, class... Ks>
inline constexpr bool holds_directive<E, TypeList<Ks...>> =
    is_directive<E> || (holds_directive<Ks> || ...);

// Whether an operand needs a rule of its own to build its value apart: one that
// holds a directive, unless a creation encloses it already.
template <class E>
inline constexpr bool apart = holds_directive<E>;
template <class T, class E>
inline constexpr bool apart<Create<T, E>> = false;
template <class... Es>
inline constexpr bool apart<Choice<Es...>> = (apart<Es> || ...);
template <class I, class H, class E>
inline constexpr bool apart<Skip<I, H, E>> = apart<E>;
template <class E, str T>
inline constexpr bool apart<Doc<E, T>> = apart<E>;

// What a directive reads its value from: an operand with a directive of its
// own that no creation encloses builds its value apart, as a rule does.
template <class E, bool Apart>
struct operand_rule {
  using type = E;
};
template <class E>
struct operand_rule<E, true> {
  using type = Ref<Rule<E{}>>;
};
template <class E>
struct operand_of : operand_rule<E, apart<E>> {};

}  // namespace cursiv::detail
