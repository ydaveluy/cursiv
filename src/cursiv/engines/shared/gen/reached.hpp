// The rules a grammar reaches from its three roots, closed to the fixpoint and
// split into terminals and the rest: the order reached is the symbol
// numbering.
#pragma once

#include <type_traits>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/InGrammar.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/lex_captures.hpp"

namespace cursiv::detail {

// The rules one expression names directly: structure reaches what its children
// reach, and three nodes name a rule themselves.
template <class E, class Kids = typename ExprShape<E>::kids>
struct refs;
template <class E>
using refs_t = typename refs<E>::type;
template <class E, class... Ks>
struct refs<E, TypeList<Ks...>> : tl_cat<refs_t<Ks>...> {};
template <class R>
struct refs<Ref<R>, TypeList<>> {
  using type = TypeList<R>;
};
template <class R>
  requires is_grammar<R>
struct refs<Ref<R>, TypeList<>> {
  static_assert(dependent_false<R>,
                "`ref<G>` names a grammar: read it with `section<G>(open, "
                "close)`, which puts G's text between two tokens of the "
                "caller");
  using type = TypeList<>;
};
// A section reaches its grammar's entry and trivia, scoped to it, and the
// tokens around.
template <class G, class O, class M, class C>
struct refs<Embed<G, Seq<O, M, C>>, TypeList<Seq<O, M, C>>>
    : tl_cat<TypeList<section_entry_t<G>>, section_ignore_t<G>,
             section_hidden_t<G>, refs_t<O>, refs_t<C>> {};
// A region's configuration names rules: they are reachable through it.
template <class I, class H, class E>
struct refs<Skip<I, H, E>, TypeList<E>> : tl_cat<I, H, refs_t<E>> {};
// A literal's guard is an expression the lexer runs, so a rule named only
// there is reachable. `void` and the unresolved sentinel are not expressions.
template <class Gd, bool Real = std::is_base_of_v<GrammarNode, Gd>>
struct guard_refs {
  using type = TypeList<>;
};
template <class Gd>
struct guard_refs<Gd, true> : refs<Gd> {};
template <str S, bool CI, class Gd>
struct refs<Lit<S, CI, Gd>, TypeList<>> : guard_refs<Gd> {};
// Conditions traverse their private rules to find lexical dependencies.
template <class R>
struct ConditionRule {};
template <class E, class Kids = typename ExprShape<E>::kids>
struct cond_refs : refs<E> {};
template <class E, class K, class... Ks>
struct cond_refs<E, TypeList<K, Ks...>>
    : tl_cat<typename cond_refs<K>::type, typename cond_refs<Ks>::type...> {};
// A section in a condition is refused by name when the image is walked.
template <class G, class E>
struct cond_refs<Embed<G, E>, TypeList<E>> {
  using type = TypeList<>;
};
template <class R>
  requires is_inline_terminal<R> || (!is_terminal_rule<R> && is_data_rule<R>)
struct cond_refs<Ref<R>, TypeList<>> {
  using type = TypeList<ConditionRule<R>>;
};
template <class E>
struct refs<Not<E>, TypeList<E>> : cond_refs<E> {};
template <class E>
struct refs<And<E>, TypeList<E>> : cond_refs<E> {};

// A rule is keyed by the name an author wrote: a body spells `Ref<Declared>`,
// and the stage is what turns that name into the rule in force here.
template <class S, class R>
using body_of = typename S::template body<declared_of_t<R>>;

template <class R>
struct as_condition {
  using type = ConditionRule<R>;
};
template <class R>
struct as_condition<ConditionRule<R>> {
  using type = ConditionRule<R>;
};
template <class L>
struct as_conditions;
template <class... Rs>
struct as_conditions<TypeList<Rs...>> {
  using type = TypeList<typename as_condition<Rs>::type...>;
};

template <class S, class R>
struct reached_refs : refs<body_of<S, R>> {};
template <class S, class R>
struct reached_refs<S, ConditionRule<R>>
    : as_conditions<refs_t<body_of<S, R>>> {};

// To the fixpoint: `Seen` is both the answer and the guard, so no rule is
// walked twice and a cycle terminates.
template <class S, class Seen, class Front>
struct closure;
template <class S, class Seen>
struct closure<S, Seen, TypeList<>> {
  using type = Seen;
};
template <class S, class Seen, class... Fs>
struct closure<S, Seen, TypeList<Fs...>> {
  using next = dedupe_t<tl_cat_t<typename reached_refs<S, Fs>::type...>>;
  using fresh = typename tl_minus<next, Seen>::type;
  using type = typename closure<S, tl_cat_t<Seen, fresh>, fresh>::type;
};

// A stage hands back the rules in force; the graph is keyed by the names an
// author wrote.
template <class L>
struct as_declared;
template <class... Rs>
struct as_declared<TypeList<Rs...>> {
  using type = TypeList<declared_of_t<Rs>...>;
};

// The three roots. Nothing else enters: a rule a grammar declares and never
// reaches is not in its language.
template <class S>
struct roots
    : dedupe<tl_cat_t<TypeList<declared_of_t<typename S::entry>>,
                      typename as_declared<typename S::ignore>::type,
                      typename as_declared<typename S::hidden>::type>> {};
template <class S>
using reached_all_t =
    typename closure<S, typename roots<S>::type, typename roots<S>::type>::type;

template <class R>
struct visible_rule {
  using type = TypeList<R>;
};
template <class R>
struct visible_rule<ConditionRule<R>> {
  using type = TypeList<>;
};
template <class L>
struct visible_rules;
template <class... Rs>
struct visible_rules<TypeList<Rs...>>
    : dedupe<tl_cat_t<typename visible_rule<Rs>::type...>> {};

template <class S>
using reached_t = typename visible_rules<reached_all_t<S>>::type;

// An inline terminal a condition names is spelled in place, unless captures
// keep its call: a call names a terminal.
template <class S, class R, bool Inline = is_inline_terminal<R>>
struct called_inline : std::false_type {};
template <class S, class R>
struct called_inline<S, R, true>
    : lex_has_captures<std::remove_cvref_t<body_of<S, R>>> {};

template <class S, class Seen, class R>
struct predicate_rule {
  using type = TypeList<>;
};
template <class S, class Seen, class R>
struct predicate_rule<S, Seen, ConditionRule<R>> {
  using type =
      cond_t<is_terminal_rule<R> &&
                 (!is_inline_terminal<R> || called_inline<S, R>::value) &&
                 !tl_has<R, Seen>::value,
             TypeList<R>, TypeList<>>;
};
template <class S, class L>
struct predicate_rules;
template <class S, class... Rs>
struct predicate_rules<S, TypeList<Rs...>>
    : tl_cat<typename predicate_rule<S, TypeList<Rs...>, Rs>::type...> {};

template <class S>
using predicate_only_t = typename predicate_rules<S, reached_all_t<S>>::type;

template <class S, class L, bool Term>
struct sift;
template <class S, class... Rs, bool Term>
struct sift<S, TypeList<Rs...>, Term>
    : tl_cat<cond_t<is_terminal_rule<
                        typename S::template rule<declared_of_t<Rs>>> == Term,
                    TypeList<Rs>, TypeList<>>...> {};

}  // namespace cursiv::detail
