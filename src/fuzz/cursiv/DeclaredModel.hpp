// The declarative grammar as a tree of positions, from its own types through
// `cursiv::detail::Normalize<G>`, independent of any engine.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/ElementKind.hpp"
#include "cursiv/GrammarModel.hpp"

namespace cursiv::fuzzer::detail::model {

using cursiv::detail::alts_list;
using cursiv::detail::cond_t;
using cursiv::detail::dedupe_t;
using cursiv::detail::dependent_false;
using cursiv::detail::ExprShape;
using cursiv::detail::index_of;
using cursiv::detail::tl_cat;
using cursiv::detail::tl_cat_t;
using cursiv::detail::tl_size;

// `refs<E>` names the direct references of one body, so every position
// holding the same expression shares one instantiation.
template <class E>
struct refs_impl;
template <class E>
using refs = refs_impl<E>;
template <class E, class Children = typename ExprShape<E>::children>
struct refs_form;
template <class E>
struct refs_form<E, TypeList<>> {
  using type = TypeList<>;
};
template <class E, class... Ks>
struct refs_form<E, TypeList<Ks...>> : tl_cat<typename refs<Ks>::type...> {};
template <class E>
struct refs_impl : refs_form<E> {};
// A region's configuration names rules: they are reachable through it.
template <class... Is, class... Hs, class E>
struct refs_impl<cursiv::detail::Skip<TypeList<Is...>, TypeList<Hs...>, E>>
    : tl_cat<TypeList<Is...>, TypeList<Hs...>, typename refs<E>::type> {};
template <class R>
struct refs_impl<cursiv::detail::Ref<R>> {
  using type = TypeList<R>;
};
// A literal's guard is an expression the lexer runs, so a rule named only
// there is still reachable. Two states of the slot are not expressions:
// `void`, and the `WordBound` sentinel the lowering resolves.
template <class Gd,
          bool Real = std::is_base_of_v<cursiv::detail::GrammarNode, Gd>>
struct guard_refs {
  using type = TypeList<>;
};
template <class Gd>
struct guard_refs<Gd, true> : refs<Gd> {};
template <str S, bool CI, class Gd>
struct refs_impl<cursiv::detail::Lit<S, CI, Gd>> : guard_refs<Gd> {};

// The primary an infix rule climbs over is reached by declaration, not by its
// synthesised body.
template <bool IsInfix, class S, class R>
struct primary_of {
  using type = TypeList<>;
};
template <class S, class R>
struct primary_of<true, S, R> {
  using type = TypeList<typename S::template rule<typename R::primary>>;
};

template <class S, class R>
struct rule_edges
    : tl_cat<
          typename refs<typename S::template body<R>>::type,
          typename primary_of<cursiv::detail::is_infix_rule<R>, S, R>::type> {};

// Computed in rounds: each round reads only the rules the previous one found,
// so every rule's edges are read once.
template <class Seen, class Cs>
struct fresh_of;
template <class Seen, class... Cs>
struct fresh_of<Seen, TypeList<Cs...>> {
  static constexpr std::size_t K = sizeof...(Cs);
  static constexpr auto kPick = [] {
    std::array<std::size_t, K + 1> idx{};
    const std::array<bool, K + 1> in{
        (index_of<Cs, Seen>::value < tl_size<Seen>::value)...};
    std::size_t n = 0;
    for (std::size_t j = 0; j < K; ++j)
      if (!in[j]) idx[n++] = j;
    return std::pair<std::size_t, std::array<std::size_t, K + 1>>{n, idx};
  }();
  template <std::size_t... I>
  static auto pick(std::index_sequence<I...>)
      -> TypeList<cursiv::detail::pack_at_t<kPick.second[I], Cs...>...>;
  using type = decltype(pick(std::make_index_sequence<kPick.first>{}));
};
template <class Seen>
struct fresh_of<Seen, TypeList<>> {
  using type = TypeList<>;
};

template <class S, class Seen, class Frontier>
struct close_rules;
template <class S, class Seen>
struct close_rules<S, Seen, TypeList<>> {
  using type = Seen;
};
template <class S, class Seen, class... Fs>
struct close_rules<S, Seen, TypeList<Fs...>> {
  using fresh = typename fresh_of<
      Seen, dedupe_t<tl_cat_t<typename rule_edges<S, Fs>::type...>>>::type;
  using type = typename close_rules<S, tl_cat_t<Seen, fresh>, fresh>::type;
};

// A reference contributes none: the rule it names carries its own.
template <class E>
struct local_impl {
  static_assert(dependent_false<E>,
                "this expression node has no position rule: the walk covers "
                "every node of expr.hpp, so a node reaching here is a new one "
                "-- give it its own specialization rather than let it "
                "contribute no position at all");
};
template <class E>
using local = local_impl<E>;
template <str S, bool CI, class Gd>
struct local_impl<cursiv::detail::Lit<S, CI, Gd>> {
  using type = TypeList<cursiv::detail::Lit<S, CI, Gd>>;
};
template <class E>
struct local_impl<cursiv::detail::Trigger<E>> : local<E> {};
// Prose takes no position: the word under it does.
template <class E, str T>
struct local_impl<cursiv::detail::Doc<E, T>> : local<E> {};
template <std::uint64_t M, bool N, str S>
struct local_impl<cursiv::detail::CpSet<M, N, S>> {
  using type = TypeList<cursiv::detail::CpSet<M, N, S>>;
};
template <>
struct local_impl<cursiv::detail::Byte> {
  using type = TypeList<cursiv::detail::Byte>;
};
template <>
struct local_impl<cursiv::detail::Eof> {
  using type = TypeList<cursiv::detail::Eof>;
};
// An implied token and a gap condition: one zero-width position each, whose
// condition stands on no position.
template <class T, class C>
struct local_impl<cursiv::detail::Implied<T, C>> {
  using type = TypeList<cursiv::detail::Implied<T, C>>;
};
template <class E>
struct local_impl<cursiv::detail::GapHas<E>> {
  using type = TypeList<cursiv::detail::GapHas<E>>;
};
template <class E>
struct local_impl<cursiv::detail::GapIs<E>> {
  using type = TypeList<cursiv::detail::GapIs<E>>;
};
template <>
struct local_impl<cursiv::detail::Epsilon> {
  using type = TypeList<>;
};
template <class D>
struct local_impl<cursiv::detail::SuperT<D>> {
  using type = TypeList<>;
};
// One position, not three: the run is lexed inside the token, so the
// operands never stand on the tape.
template <class O, class Cl>
struct local_impl<cursiv::detail::Through<O, Cl>> {
  using type = TypeList<cursiv::detail::Through<O, Cl>>;
};
template <class... Es>
struct local_impl<cursiv::detail::Seq<Es...>>
    : tl_cat<TypeList<cursiv::detail::Seq<Es...>>,
             typename local<Es>::type...> {};
template <class... Es>
struct local_impl<cursiv::detail::Choice<Es...>>
    : tl_cat<TypeList<cursiv::detail::Choice<Es...>>,
             typename local<Es>::type...> {};
template <class... Es>
struct local_impl<cursiv::detail::Unordered<Es...>>
    : tl_cat<TypeList<cursiv::detail::Unordered<Es...>>,
             typename local<Es>::type...> {};
template <std::size_t Mn, std::size_t Mx, class E>
struct local_impl<cursiv::detail::Repeat<Mn, Mx, E>>
    : tl_cat<TypeList<cursiv::detail::Repeat<Mn, Mx, E>>,
             typename local<E>::type> {};
template <class E>
struct local_impl<cursiv::detail::Not<E>>
    : tl_cat<TypeList<cursiv::detail::Not<E>>, typename local<E>::type> {};
template <class E>
struct local_impl<cursiv::detail::And<E>>
    : tl_cat<TypeList<cursiv::detail::And<E>>, typename local<E>::type> {};
template <auto P, class E>
struct local_impl<cursiv::detail::Assign<P, E>>
    : tl_cat<TypeList<cursiv::detail::Assign<P, E>>, typename local<E>::type> {
};
template <auto P, class E>
struct local_impl<cursiv::detail::Append<P, E>>
    : tl_cat<TypeList<cursiv::detail::Append<P, E>>, typename local<E>::type> {
};
template <auto P, class E>
struct local_impl<cursiv::detail::Flag<P, E>>
    : tl_cat<TypeList<cursiv::detail::Flag<P, E>>, typename local<E>::type> {};
template <class T, class E>
struct local_impl<cursiv::detail::Create<T, E>>
    : tl_cat<TypeList<cursiv::detail::Create<T, E>>, typename local<E>::type> {
};
template <class I, class H, class E>
struct local_impl<cursiv::detail::Skip<I, H, E>>
    : tl_cat<TypeList<cursiv::detail::Skip<I, H, E>>, typename local<E>::type> {
};
template <class T, auto P, class E>
struct local_impl<cursiv::detail::Nest<T, P, E>>
    : tl_cat<TypeList<cursiv::detail::Nest<T, P, E>>, typename local<E>::type> {
};
template <class R>
struct local_impl<cursiv::detail::Ref<R>> {
  using type = TypeList<>;
};

// A terminal is one token and an infix rule builds its own shape: neither's
// body is walked. The climb's operators are positions of their own, named
// by the level table rather than by the synthesised body.
template <class R>
inline constexpr bool atomic_rule =
    is_terminal_rule<R> || cursiv::detail::is_infix_rule<R>;

// An operator may be spelled, or it may call a terminal or a data rule. A
// call is no position of its own -- the rule it names carries it, as
// everywhere else.
template <class E>
struct op_position {
  using type = E;
};
template <class R>
struct op_position<cursiv::detail::Ref<R>> {
  using type = cursiv::detail::declared_of_t<R>;
};
template <class L>
struct ops_of;
template <class... Es>
struct ops_of<TypeList<Es...>> {
  using type = TypeList<typename op_position<Es>::type...>;
};

template <class L>
struct level_ops;
template <template <class...> class Lv, class... Ls>
struct level_ops<Lv<Ls...>>
    : tl_cat<typename ops_of<
          typename alts_list<typename Ls::op_expr>::type>::type...> {};
template <bool IsInfix, class R>
struct infix_ops {
  using type = TypeList<>;
};
template <class R>
struct infix_ops<true, R> : level_ops<typename R::levels> {};

template <bool Atomic, class S, class R>
struct rule_positions {
  using type = TypeList<>;
};
template <class S, class R>
struct rule_positions<false, S, R> : local<typename S::template body<R>> {};
template <class S, class R>
struct positions_of
    : tl_cat<TypeList<R>,
             typename infix_ops<cursiv::detail::is_infix_rule<R>, R>::type,
             typename rule_positions<atomic_rule<R>, S, R>::type> {};
template <class S, class L>
struct positions_all;
template <class S, class... Rs>
struct positions_all<S, TypeList<Rs...>>
    : tl_cat<typename positions_of<S, Rs>::type...> {};

// A rule is told apart by its own predicates; every other node states its
// kind here, so a combinator added without a line fails to compile rather
// than falling into a permissive default.
template <class E>
struct shape_kind {
  static_assert(dependent_false<E>,
                "this expression node states no kind: give it a line rather "
                "than let it read as a group");
};
template <str S, bool CI, class Gd>
struct shape_kind<cursiv::detail::Lit<S, CI, Gd>> {
  static constexpr ElementKind value = ElementKind::literal;
};
template <std::uint64_t M, bool N, str S>
struct shape_kind<cursiv::detail::CpSet<M, N, S>> {
  static constexpr ElementKind value = ElementKind::character_range;
};
template <>
struct shape_kind<cursiv::detail::Byte> {
  static constexpr ElementKind value = ElementKind::any_character;
};
template <>
struct shape_kind<cursiv::detail::AnyChar> {
  static constexpr ElementKind value = ElementKind::any_character;
};
template <class O, class Cl>
struct shape_kind<cursiv::detail::Through<O, Cl>> {
  static constexpr ElementKind value = ElementKind::through;
};
template <class... Es>
struct shape_kind<cursiv::detail::Seq<Es...>> {
  static constexpr ElementKind value = ElementKind::group;
};
template <class... Es>
struct shape_kind<cursiv::detail::Choice<Es...>> {
  static constexpr ElementKind value = ElementKind::ordered_choice;
};
template <class... Es>
struct shape_kind<cursiv::detail::Unordered<Es...>> {
  static constexpr ElementKind value = ElementKind::unordered_group;
};
template <std::size_t Mn, std::size_t Mx, class E>
struct shape_kind<cursiv::detail::Repeat<Mn, Mx, E>> {
  static constexpr ElementKind value = ElementKind::repetition;
};
template <>
struct shape_kind<cursiv::detail::Eof> {
  static constexpr ElementKind value = ElementKind::not_predicate;
};
template <class E>
struct shape_kind<cursiv::detail::Not<E>> {
  static constexpr ElementKind value = ElementKind::not_predicate;
};
template <class E>
struct shape_kind<cursiv::detail::And<E>> {
  static constexpr ElementKind value = ElementKind::and_predicate;
};
// Zero-width where a condition holds, which the walk cannot impose: an
// assertion.
template <class T, class C>
struct shape_kind<cursiv::detail::Implied<T, C>>
    : shape_kind<cursiv::detail::And<C>> {};
template <class E>
struct shape_kind<cursiv::detail::GapHas<E>>
    : shape_kind<cursiv::detail::And<E>> {};
template <class E>
struct shape_kind<cursiv::detail::GapIs<E>>
    : shape_kind<cursiv::detail::And<E>> {};
template <auto P, class E>
struct shape_kind<cursiv::detail::Assign<P, E>> {
  static constexpr ElementKind value = ElementKind::assignment;
};
template <auto P, class E>
struct shape_kind<cursiv::detail::Append<P, E>> {
  static constexpr ElementKind value = ElementKind::assignment;
};
template <auto P, class E>
struct shape_kind<cursiv::detail::Flag<P, E>> {
  static constexpr ElementKind value = ElementKind::assignment;
};
template <class T, class E>
struct shape_kind<cursiv::detail::Create<T, E>> {
  static constexpr ElementKind value = ElementKind::create;
};
template <class T, auto P, class E>
struct shape_kind<cursiv::detail::Nest<T, P, E>> {
  static constexpr ElementKind value = ElementKind::nest;
};
template <class I, class H, class E>
struct shape_kind<cursiv::detail::Skip<I, H, E>> {
  static constexpr ElementKind value = ElementKind::skip_region;
};

template <class E>
consteval ElementKind kind_of() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_terminal_rule<T>)
      return ElementKind::terminal_rule;
    else if constexpr (cursiv::detail::is_infix_rule<T>)
      return ElementKind::infix_rule;
    else if constexpr (cursiv::detail::is_data_rule<T>)
      return ElementKind::data_type_rule;
    else
      return ElementKind::parser_rule;
  } else {
    return shape_kind<T>::value;
  }
}

template <class E>
struct text_of {
  static constexpr std::string_view value{};
};
template <str S, bool CI, class Gd>
struct text_of<cursiv::detail::Lit<S, CI, Gd>> {
  static constexpr std::string_view value = S.view();
};

// Under the name an override does not replace, so two stages name one rule.
template <class E>
consteval std::string_view name_of() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_terminal_rule<T>)
      return cursiv::detail::type_name<cursiv::detail::declared_of_t<T>>();
  }
  return {};
}

template <class E>
struct feature_of {
  static constexpr std::string_view value{};
};
template <auto Sp, class E>
struct feature_of<cursiv::detail::Assign<Sp, E>> {
  static constexpr std::string_view value =
      cursiv::detail::member_identifier<cursiv::detail::spec_ptr<Sp>>();
};
template <auto Sp, class E>
struct feature_of<cursiv::detail::Append<Sp, E>> {
  static constexpr std::string_view value =
      cursiv::detail::member_identifier<cursiv::detail::spec_ptr<Sp>>();
};
template <auto P, class E>
struct feature_of<cursiv::detail::Flag<P, E>> {
  static constexpr std::string_view value =
      cursiv::detail::member_identifier<P>();
};

template <class E>
struct bounds_of {
  static constexpr std::uint16_t min = 0;
  static constexpr std::uint16_t max = 0;
};
template <std::size_t Mn, std::size_t Mx, class E>
struct bounds_of<cursiv::detail::Repeat<Mn, Mx, E>> {
  static constexpr std::uint16_t min =
      Mn >= kModelUnbounded ? kModelUnbounded : static_cast<std::uint16_t>(Mn);
  static constexpr std::uint16_t max =
      Mx >= kModelUnbounded ? kModelUnbounded : static_cast<std::uint16_t>(Mx);
};

// What a position contains: a rule, its body; anything else, what its shape
// declares. The graph has cycles, so a reader guards its own descent.
template <class S, class E, bool IsRule = requires { typename E::kind; }>
struct children_of {
  using type = typename ExprShape<E>::children;
};
template <class S, class R>
struct children_of<S, R, true> {
  using type = cond_t<atomic_rule<R>, TypeList<>,
                      TypeList<typename S::template body<R>>>;
};

// A child resolves to its own row -- or, for a call, to the row of the rule it
// names: a `Ref<R>` spells no position of its own. What resolves to nothing at
// all (an `Epsilon`, a `super<>`) is not a child.
template <class K>
struct child_target {
  using type = K;
};
template <class R>
struct child_target<cursiv::detail::Ref<R>> {
  using type = R;
};
// Prose spells no position either: the word under it is the child.
template <class E, str T>
struct child_target<cursiv::detail::Doc<E, T>> : child_target<E> {};
template <class L, class K>
inline constexpr ElementId row_of_child =
    index_of<typename child_target<std::remove_cvref_t<K>>::type, L>::value <
            tl_size<L>::value
        ? static_cast<ElementId>(
              index_of<typename child_target<std::remove_cvref_t<K>>::type,
                       L>::value)
        : kNoElement;

struct PositionRow {
  ElementKind kind = ElementKind::group;
  std::uint16_t textOff = 0, textLen = 0;
  std::uint16_t featOff = 0, featLen = 0;
  std::uint16_t nameOff = 0, nameLen = 0;
  std::uint16_t min = 0, max = 0;
  std::uint16_t childAt = 0, childCount = 0;
};

template <class S, class L, class Ks>
struct child_ids;
template <class S, class L, class... Ks>
struct child_ids<S, L, TypeList<Ks...>> {
  static constexpr std::size_t kMax = sizeof...(Ks) + 1;
  static constexpr auto value = [] {
    std::array<ElementId, kMax> a{};
    std::size_t n = 0;
    ((row_of_child<L, Ks> != kNoElement ? (void)(a[n++] = row_of_child<L, Ks>)
                                        : (void)0),
     ...);
    return std::pair<std::size_t, std::array<ElementId, kMax>>{n, a};
  }();
};

// One blob and offsets into it: a table of views would be a table of
// pointers the loader has to relocate. A slice already present is reused,
// so an empty one matches at offset zero and costs the pass nothing.
template <class S, class L>
struct table_of;
template <class S, class... Es>
struct table_of<S, TypeList<Es...>> {
  using L = TypeList<Es...>;
  static constexpr std::size_t kN = sizeof...(Es);
  static constexpr std::size_t kMax =
      (text_of<Es>::value.size() + ... + std::size_t{0}) +
      (feature_of<Es>::value.size() + ... + std::size_t{0}) +
      (name_of<Es>().size() + ... + std::size_t{0}) + 2;
  static constexpr std::size_t kChildMax =
      (child_ids<S, L, typename children_of<S, Es>::type>::value.first + ... +
       std::size_t{1});
  // A row addresses the blob and the children in 16 bits: the bound is checked
  // here rather than found as a truncated offset.
  static_assert(kMax <= 0xFFFFu && kChildMax <= 0xFFFFu,
                "cursiv: this grammar's declared strings or children exceed "
                "what a PositionRow offset can address");

  struct Layout {
    std::array<char, kMax + 1> blob{};
    std::size_t used = 0;
    std::array<PositionRow, kN + 1> rows{};
    std::array<ElementId, kChildMax + 1> children{};
    std::size_t nchildren = 0;
  };

  static consteval Layout compute() {
    Layout r{};
    const std::string_view texts[kN + 1] = {text_of<Es>::value..., {}};
    const std::string_view feats[kN + 1] = {feature_of<Es>::value..., {}};
    const std::string_view names[kN + 1] = {name_of<Es>()..., {}};
    const ElementKind kinds[kN + 1] = {kind_of<Es>()..., ElementKind::group};
    const std::uint16_t mins[kN + 1] = {
        bounds_of<std::remove_cvref_t<Es>>::min..., 0};
    const std::uint16_t maxs[kN + 1] = {
        bounds_of<std::remove_cvref_t<Es>>::max..., 0};
    // Appended, never searched: `kMax` already holds every string, and a
    // search would make the pass quadratic.
    const auto place = [&r](std::string_view l) consteval {
      const std::size_t at = r.used;
      for (const char c : l) r.blob[r.used++] = c;
      return at;
    };
    std::size_t i = 0;
    ((
         r.rows[i] =
             PositionRow{
                 kinds[i], static_cast<std::uint16_t>(place(texts[i])),
                 static_cast<std::uint16_t>(texts[i].size()),
                 static_cast<std::uint16_t>(place(feats[i])),
                 static_cast<std::uint16_t>(feats[i].size()),
                 static_cast<std::uint16_t>(place(names[i])),
                 static_cast<std::uint16_t>(names[i].size()), mins[i], maxs[i],
                 static_cast<std::uint16_t>(r.nchildren),
                 static_cast<std::uint16_t>(
                     child_ids<S, L, typename children_of<S, Es>::type>::value
                         .first)},
         [&] {
           const auto& k =
               child_ids<S, L, typename children_of<S, Es>::type>::value;
           for (std::size_t j = 0; j < k.first; ++j)
             r.children[r.nchildren++] = k.second[j];
         }(),
         ++i),
     ...);
    return r;
  }

  // One evaluation: asking for it once per member is what would multiply it.
  static constexpr Layout kLayout = compute();
};

template <class G>
struct positions_for {
  using stage = cursiv::detail::Normalize<G>;
  using seeds =
      dedupe_t<tl_cat_t<TypeList<typename stage::entry>,
                        typename stage::ignored, typename stage::hidden>>;
  using rules = typename close_rules<stage, seeds, seeds>::type;
  using type = dedupe_t<typename positions_all<stage, rules>::type>;
};

// The row a type stands at, and the row a call resolves to: `Ref<R>`
// spells no position of its own -- the rule it names carries it.
template <class G, class T>
inline constexpr ElementId row_of =
    index_of<std::remove_cvref_t<T>, typename positions_for<G>::type>::value <
            tl_size<typename positions_for<G>::type>::value
        ? static_cast<ElementId>(
              index_of<std::remove_cvref_t<T>,
                       typename positions_for<G>::type>::value)
        : kNoElement;
// A call's row, by the name the body spells first, then by its resolved type,
// under which an infix primary stands.
template <class G, class R>
inline constexpr ElementId call_row =
    row_of<G, R> != kNoElement
        ? row_of<G, R>
        : row_of<G, typename cursiv::detail::Normalize<G>::template rule<R>>;

// An infix operator is spelled, or it calls the terminal or the data rule
// that reads it -- and then it is that rule's row, like any other call.
template <class G, class E>
inline constexpr ElementId op_row = row_of<G, E>;
template <class G, class R>
inline constexpr ElementId op_row<G, cursiv::detail::Ref<R>> = call_row<G, R>;

}  // namespace cursiv::fuzzer::detail::model

namespace cursiv::fuzzer::detail {

// One instance per grammar, and it holds no engine: every answer is a
// slice of a table the grammar's own types produced.
template <class G>
struct DeclaredModel final : GrammarModel {
 private:
  using stage = cursiv::detail::Normalize<G>;
  using positions = typename model::positions_for<G>::type;
  using table = model::table_of<stage, positions>;
  static constexpr auto& kLayout = table::kLayout;

  static constexpr bool holds(ElementId e) { return e < table::kN; }

 public:
  [[nodiscard]] ElementId positionCount() const noexcept override {
    return static_cast<ElementId>(table::kN);
  }
  [[nodiscard]] ElementId entryRule() const noexcept override {
    return static_cast<ElementId>(
        cursiv::detail::index_of<typename stage::entry, positions>::value);
  }
  [[nodiscard]] ElementKind kindOf(ElementId e) const noexcept override {
    return holds(e) ? kLayout.rows[e].kind : ElementKind::group;
  }
  [[nodiscard]] std::span<const ElementId> childrenOf(
      ElementId e) const noexcept override {
    if (!holds(e)) return {};
    return {kLayout.children.data() + kLayout.rows[e].childAt,
            kLayout.rows[e].childCount};
  }
  [[nodiscard]] std::string_view textOf(ElementId e) const noexcept override {
    if (!holds(e)) return {};
    return {kLayout.blob.data() + kLayout.rows[e].textOff,
            kLayout.rows[e].textLen};
  }
  [[nodiscard]] std::string_view nameOf(ElementId e) const noexcept override {
    if (!holds(e)) return {};
    return {kLayout.blob.data() + kLayout.rows[e].nameOff,
            kLayout.rows[e].nameLen};
  }
  [[nodiscard]] std::string_view featureOf(
      ElementId e) const noexcept override {
    if (!holds(e)) return {};
    return {kLayout.blob.data() + kLayout.rows[e].featOff,
            kLayout.rows[e].featLen};
  }
  [[nodiscard]] std::uint16_t minOf(ElementId e) const noexcept override {
    return holds(e) ? kLayout.rows[e].min : std::uint16_t{0};
  }
  [[nodiscard]] std::uint16_t maxOf(ElementId e) const noexcept override {
    return holds(e) ? kLayout.rows[e].max : std::uint16_t{0};
  }
};

template <class G>
inline constexpr DeclaredModel<G> kDeclaredModel{};

}  // namespace cursiv::fuzzer::detail
