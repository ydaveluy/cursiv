// A rule of grammar G read inside the section another grammar opens with
// `section<G>`: G's rule in force -- G's overrides, never the caller's -- over
// a body whose names and keywords stay G's.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv {
namespace detail {

template <class G, class X>
using in_force_t = typename ResolveOverride<G>::template rule<X>;

template <class G, class E, class Kids = typename ExprShape<E>::kids>
struct scoped;
template <class G, class E>
using scoped_t = typename scoped<G, E>::type;
template <class G, class E, class... Ks>
struct scoped<G, E, TypeList<Ks...>> {
  using type = typename ExprShape<E>::template rebind<scoped_t<G, Ks>...>;
};

template <class G, class R, bool Whole = is_grammar<R>>
struct scoped_name {
  using type = InGrammar<G, declared_of_t<R>>;
};
template <class G, class R>
struct scoped_name<G, R, true> {
  using type = R;
};
template <class G, class R>
struct scoped<G, Ref<R>, TypeList<>> {
  using type = Ref<typename scoped_name<G, R>::type>;
};

template <class G, class L>
struct scoped_list;
template <class G, class... Rs>
struct scoped_list<G, TypeList<Rs...>> {
  using type = TypeList<InGrammar<G, declared_of_t<Rs>>...>;
};
template <class G, class I, class H, class E>
struct scoped<G, Skip<I, H, E>, TypeList<E>> {
  using type = Skip<typename scoped_list<G, I>::type,
                    typename scoped_list<G, H>::type, scoped_t<G, E>>;
};

// A keyword takes G's word boundary, resolved here: the caller's is another.
template <class G, str S, bool CI, class Gd>
struct scoped_guard {
  using type = scoped_t<G, Gd>;
};
template <class G, str S, bool CI>
struct scoped_guard<G, S, CI, void> {
  using type = void;
};
template <class G, str S, bool CI>
struct scoped_guard<G, S, CI, WordBound> {
  using bound =
      std::remove_cvref_t<decltype(ResolveOverride<G>::word_boundary)>;
  using type = cond_t<word_final_in<S, bound, CI>(), Not<bound>, void>;
};
template <class G, str S, bool CI, class Gd>
struct scoped<G, Lit<S, CI, Gd>, TypeList<>> {
  using type = Lit<S, CI, typename scoped_guard<G, S, CI, Gd>::type>;
};

template <class G, class E>
struct scoped<G, GapHas<E>, TypeList<>> {
  using type = GapHas<scoped_t<G, E>>;
};
template <class G, class E>
struct scoped<G, GapIs<E>, TypeList<>> {
  using type = GapIs<scoped_t<G, E>>;
};
template <class G, class T, class C>
struct scoped<G, Implied<T, C>, TypeList<>> {
  using type = Implied<scoped_t<G, T>, scoped_t<G, C>>;
};

template <class G, class E>
struct unindented {
  static_assert(dependent_false<G>,
                "a grammar embedded with `section<>` cannot use indentation: "
                "its lines would be measured against the outer document. "
                "Embed a grammar that declares no `indent`/`dedent`, or parse "
                "the indented language on its own");
  using type = E;
};
template <class G>
struct scoped<G, Indent, TypeList<>> : unindented<G, Indent> {};
template <class G>
struct scoped<G, Dedent, TypeList<>> : unindented<G, Dedent> {};

template <class G, class L>
struct scoped_level;
template <class G, class Op>
struct scoped_level<G, LeftAssoc<Op>> {
  using type = LeftAssoc<scoped_t<G, Op>>;
};
template <class G, class Op>
struct scoped_level<G, RightAssoc<Op>> {
  using type = RightAssoc<scoped_t<G, Op>>;
};
template <class G, class Op>
struct scoped_level<G, NonAssoc<Op>> {
  using type = NonAssoc<scoped_t<G, Op>>;
};
template <class G, Association A, class N, auto O, auto X, class Op>
struct scoped_level<G, UnaryLevel<A, N, O, X, Op>> {
  using type = UnaryLevel<A, N, O, X, scoped_t<G, Op>>;
};
template <class G, class L>
struct scoped_levels;
template <class G, class... Ls>
struct scoped_levels<G, Levels<Ls...>> {
  using type = Levels<typename scoped_level<G, Ls>::type...>;
};

// The declaration a converter comes from: the nearest override that states
// one, else the rule as declared.
template <class Eff, class D = ov::slot_decl_t<Eff>>
struct slot_holder {
  using type = D;
};
template <class Eff>
struct slot_holder<Eff, void> {
  using type = declared_of_t<Eff>;
};

template <class G, class X, class Eff = in_force_t<G, X>,
          class Kind = typename Eff::kind>
struct in_grammar_rule {
  using kind = Kind;
  using slot = typename slot_holder<Eff>::type::slot;
  static constexpr auto body =
      scoped_t<G, std::remove_cv_t<decltype(Eff::body)>>{};
};
template <class G, class X, class Eff>
struct in_grammar_rule<G, X, Eff, infix_kind> {
  using kind = infix_kind;
  using slot = TypeList<>;
  using primary = InGrammar<G, declared_of_t<typename Eff::primary>>;
  using node_type = typename Eff::node_type;
  using levels = typename scoped_levels<G, typename Eff::levels>::type;
  static constexpr auto left_pointer = Eff::left_pointer;
  static constexpr auto op_pointer = Eff::op_pointer;
  static constexpr auto right_pointer = Eff::right_pointer;
  static constexpr auto body =
      scoped_t<G, std::remove_cv_t<decltype(Eff::body)>>{};
};

template <class G, class X>
struct in_grammar_name {
  static constexpr std::string_view grammar = type_name<G>();
  static constexpr std::string_view rule = type_name<X>();
  static constexpr auto chars = [] {
    std::array<char, grammar.size() + 2 + rule.size()> a{};
    std::size_t at = 0;
    for (const char c : grammar) a[at++] = c;
    a[at++] = ':';
    a[at++] = ':';
    for (const char c : rule) a[at++] = c;
    return a;
  }();
};

}  // namespace detail

// No `declared`: a rule of G read in a section is a rule of its own, never
// the caller's rule of the same name.
template <class G, class X>
struct InGrammar : detail::in_grammar_rule<G, X> {
  static_assert(
      std::is_same_v<std::remove_cvref_t<decltype(G::indent_open)>, Epsilon> &&
          std::is_same_v<std::remove_cvref_t<decltype(G::indent_close)>,
                         Epsilon> &&
          std::is_void_v<typename G::indent_newline>,
      "a grammar embedded with `section<>` cannot use indentation: its lines "
      "would be measured against the outer document. Embed a grammar that "
      "declares no `indent_open`, `indent_close` or `indent_newline`");
};

template <class G, class X>
inline constexpr std::string_view anonymous_rule_label<InGrammar<G, X>> =
    anonymous_rule_label<X>;
template <class G, class X>
inline constexpr bool is_inline_terminal<InGrammar<G, X>> =
    is_inline_terminal<X>;

namespace detail {
template <class G, class E = named_rule_t<G>>
struct section_entry {
  static_assert(!is_terminal_rule<E>,
                "a grammar embedded with `section<>` has a terminal as its "
                "entry, but a section is parsed by rules: give the grammar an "
                "entry rule that uses the terminal");
  using type = E;
};
}  // namespace detail

// The section `section<G>` opens: G's entry, and the trivia G reads.
template <class G>
using section_entry_t = typename detail::section_entry<G>::type;
template <class G>
using section_ignored_t =
    typename detail::scoped_list<G, typename ResolveOverride<G>::ignored>::type;
template <class G>
using section_hidden_t =
    typename detail::scoped_list<G, typename ResolveOverride<G>::hidden>::type;

namespace detail {

// A rule's type is what G's own deduction answers, the rules already on the
// way kept: a grammar that embeds itself adopts as a rule that calls itself.
template <class G, class X, class Seen>
struct rule_type<InGrammar<G, X>, terminal_kind, Seen>
    : rule_type<in_force_t<G, X>, terminal_kind, Seen> {};
template <class G, class X, class Seen>
struct rule_type<InGrammar<G, X>, node_kind, Seen>
    : rule_type<in_force_t<G, X>, node_kind, Seen> {};
template <class G, class X, class Seen>
struct rule_type<InGrammar<G, X>, infix_kind, Seen>
    : rule_type<in_force_t<G, X>, infix_kind, Seen> {};
// ...so a deduction entering it walks the body of the rule in force too.
template <class G, class X>
struct deduction_calls<InGrammar<G, X>>
    : dedupe<tl_cat_t<typename rule_calls<InGrammar<G, X>>::type,
                      typename rule_calls<in_force_t<G, X>>::type>> {};

// Named after G: `Json::Value`.
template <class G, class X>
inline constexpr std::string_view kTypeNameView<InGrammar<G, X>> = {
    in_grammar_name<G, X>::chars.data(), in_grammar_name<G, X>::chars.size()};

// The grammar a rule belongs to: the caller's, `void`, or the one embedded.
template <class R>
struct home_of {
  using type = void;
};
template <class G, class X>
struct home_of<InGrammar<G, X>> {
  using type = G;
};

}  // namespace detail
}  // namespace cursiv
