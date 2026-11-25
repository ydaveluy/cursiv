// Rule and scanner dependencies reachable from the entry and trivia.
#pragma once

#include <type_traits>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/InGrammar.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// Dependencies named by an expression and its children.
template <class E, class Children = typename detail::ExprShape<E>::children>
struct refs;
template <class E>
using refs_t = typename refs<E>::type;
template <class E, class... Ks>
struct refs<E, TypeList<Ks...>> : detail::tl_cat<refs_t<Ks>...> {};
// Scanner callbacks share the stable dependency index used by both compilers.
template <auto F>
struct refs<detail::Scan<F>, TypeList<>> { using type = TypeList<detail::Scan<F>>; };
template <class R>
struct refs<detail::Ref<R>, TypeList<>> {
  using type = TypeList<R>;
};
template <class R>
  requires detail::is_grammar<R>
struct refs<detail::Ref<R>, TypeList<>> {
  static_assert(detail::dependent_false<R>,
                "`ref<G>` names a grammar: read it with `section<G>(open, "
                "close)`, which puts G's text between two tokens of the "
                "caller");
  using type = TypeList<>;
};
// A section reaches its grammar's entry and trivia, scoped to it, and the
// tokens around.
template <class G, class O, class M, class C>
struct refs<detail::Embed<G, detail::Seq<O, M, C>>, TypeList<detail::Seq<O, M, C>>>
    : detail::tl_cat<TypeList<detail::section_entry_t<G>>, detail::section_ignored_t<G>,
             detail::section_hidden_t<G>, refs_t<O>, refs_t<C>> {};
// A region's configuration names rules: they are reachable through it.
template <class I, class H, class E>
struct refs<detail::Skip<I, H, E>, TypeList<E>> : detail::tl_cat<I, H, refs_t<E>> {};
// A literal's guard is an expression the lexer runs, so a rule named only
// there is reachable. `void` and the unresolved sentinel are not expressions.
template <class Gd, bool Real = std::is_base_of_v<detail::GrammarNode, Gd>>
struct guard_refs {
  using type = TypeList<>;
};
template <class Gd>
struct guard_refs<Gd, true> : refs<Gd> {};
template <str S, bool CI, class Gd, bool Keyword>
struct refs<detail::Lit<S, CI, Gd, Keyword>, TypeList<>> : guard_refs<Gd> {};
// Conditions traverse their private rules to find lexical dependencies.
template <class R>
struct ConditionRule {};
template <class R>
struct as_condition {
  using type = ConditionRule<R>;
};
template <class R>
struct as_condition<ConditionRule<R>> {
  using type = ConditionRule<R>;
};
template <auto F>
struct as_condition<detail::Scan<F>> { using type = detail::Scan<F>; };
template <class L>
struct as_conditions;
template <class... Rs>
struct as_conditions<TypeList<Rs...>> {
  using type = TypeList<typename as_condition<Rs>::type...>;
};

template <class E, class Children = typename detail::ExprShape<E>::children>
struct cond_refs : refs<E> {};
template <class E, class K, class... Ks>
struct cond_refs<E, TypeList<K, Ks...>>
    : detail::tl_cat<typename cond_refs<K>::type, typename cond_refs<Ks>::type...> {};
// A section in a condition is refused by name when the grammar is lowered.
template <class G, class E>
struct cond_refs<detail::Embed<G, E>, TypeList<E>> {
  using type = TypeList<>;
};
// A region's trivia are read by the condition, even where nothing else names
// them.
template <class I, class H, class E>
struct cond_refs<detail::Skip<I, H, E>, TypeList<E>>
    : detail::tl_cat<typename as_conditions<I>::type, typename as_conditions<H>::type,
             typename cond_refs<E>::type> {};
template <class R>
  requires detail::is_inline_terminal<R> || (!kIsTerminalRule<R> && detail::is_data_rule<R>)
struct cond_refs<detail::Ref<R>, TypeList<>> {
  using type = TypeList<ConditionRule<R>>;
};
template <class E>
struct refs<detail::Not<E>, TypeList<E>> : cond_refs<E> {};
template <class E>
struct refs<detail::And<E>, TypeList<E>> : cond_refs<E> {};

// A rule is keyed by the name an author wrote: a body spells `Ref<Declared>`,
// and the stage is what turns that name into the rule in force here.
template <class S, class R>
using body_of = typename S::template body<detail::declared_of_t<R>>;

template <class S, class R>
struct reached_refs : refs<body_of<S, R>> {};
template <class S, class R>
struct reached_refs<S, ConditionRule<R>>
    : as_conditions<refs_t<body_of<S, R>>> {};

template <class S, auto F>
struct reached_refs<S, detail::Scan<F>> { using type = TypeList<>; };

// To the fixpoint: `Seen` is both the answer and the guard, so no rule is
// walked twice and a cycle terminates.
template <class S>
struct reached_calls {
  template <class R>
  using of = reached_refs<S, R>;
};
template <class S, class Seen, class Front>
using closure =
    cursiv::detail::calls_closure<reached_calls<S>::template of, Seen, Front>;

// A stage hands back the rules in force; the graph is keyed by the names an
// author wrote.
template <class L>
struct as_declared;
template <class... Rs>
struct as_declared<TypeList<Rs...>> {
  using type = TypeList<detail::declared_of_t<Rs>...>;
};

// The three roots. Nothing else enters: a rule a grammar declares and never
// reaches is not in its language.
template <class S>
struct roots
    : detail::dedupe<detail::tl_cat_t<TypeList<detail::declared_of_t<typename S::entry>>,
                      typename as_declared<typename S::ignored>::type,
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
template <auto F>
struct visible_rule<detail::Scan<F>> { using type = TypeList<>; };
template <class L>
struct visible_rules;
template <class... Rs>
struct visible_rules<TypeList<Rs...>>
    : detail::tl_cat<typename visible_rule<Rs>::type...> {};

template <class S>
using reached_t = typename visible_rules<reached_all_t<S>>::type;

}  // namespace cursiv::engine
