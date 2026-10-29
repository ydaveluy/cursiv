// A left recursion is read as the loop of the rule a call starts with. The
// walk from that rule meets its bases and its turns in the order they rank;
// the loop is laid as folds, a base followed by the turns that apply to it,
// nested as the recursion nests, so a descent chooses among them as the
// election does. An alternative whose call stands inside or behind optional
// items is first read as the alternatives they spell.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/LeadGraph.hpp"
#include "cursiv/engines/shared/gen/optional_shape.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"

namespace cursiv::detail::llow {

template <class E>
struct fold_alts {
  using type = TypeList<E>;
};
template <class... Es>
struct fold_alts<Choice<Es...>> : tl_cat<typename fold_alts<Es>::type...> {};

template <class L>
struct make_seq;
template <>
struct make_seq<TypeList<>> {
  using type = Epsilon;
};
template <class E>
struct make_seq<TypeList<E>> {
  using type = E;
};
template <class E, class F, class... Es>
struct make_seq<TypeList<E, F, Es...>> {
  using type = Seq<E, F, Es...>;
};
template <class E>
struct pieces_of {
  using type = TypeList<E>;
};
template <>
struct pieces_of<Epsilon> {
  using type = TypeList<>;
};
template <class... Es>
struct pieces_of<Seq<Es...>> : tl_cat<typename pieces_of<Es>::type...> {};
template <class... Es>
using join_t =
    typename make_seq<tl_cat_t<typename pieces_of<Es>::type...>>::type;

// `E` closed over its rule: the conversion of a value rule, and for the rule a
// fold belongs to, its close. A step reaches back over what the fold has read.
template <class R, class E, bool Step, bool Hands>
struct Closed : GrammarNode {};
template <class E>
struct Turns : GrammarNode {};

}  // namespace cursiv::detail::llow

namespace cursiv::detail {

template <class R, class E, bool Step, bool Hands>
struct ExprShape<llow::Closed<R, E, Step, Hands>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = llow::Closed<R, K..., Step, Hands>;
};
template <class E>
struct ExprShape<llow::Turns<E>> {
  using kids = TypeList<E>;
  template <class... K>
  using rebind = llow::Turns<K...>;
};
template <class R, class E, bool Step, bool Hands>
inline constexpr bool transparent<llow::Closed<R, E, Step, Hands>> = true;
template <class R, class E, bool Step, bool Hands>
inline constexpr bool grouping<llow::Closed<R, E, Step, Hands>> = true;
template <class E>
inline constexpr bool transparent<llow::Turns<E>> = true;
template <class E>
inline constexpr bool grouping<llow::Turns<E>> = true;

}  // namespace cursiv::detail

namespace cursiv::detail::llow {

template <class Lead, class Recipe>
struct LeadArm {
  using lead = Lead;
  using recipe = Recipe;
  static constexpr bool unit = std::is_same_v<Recipe, Epsilon>;
};
template <class E>
struct BaseArm {
  using expr = E;
};

template <class G, class O, class A>
struct operand_lead : std::false_type {};
template <class G, class O, class X>
  requires(!is_grammar<X> && G::template same<O, X>)
struct operand_lead<G, O, Ref<X>> : std::true_type {};
template <class G, class O, class L>
struct operand_leads;
template <class G, class O, class... As>
struct operand_leads<G, O, TypeList<As...>>
    : std::bool_constant<(operand_lead<G, O, As>::value || ...)> {};

template <class G, class O, class T, auto P, class Rest, class Alt>
struct operand_arm;
template <class G, class O, class T, auto P, class... Es, class Alt>
struct operand_arm<G, O, T, P, TypeList<Es...>, Alt> {
  using type = TypeList<BaseArm<Create<T, Seq<Assign<P, Alt>, Es...>>>>;
};
template <class G, class O, class T, auto P, class... Es, class X>
  requires(!is_grammar<X> && G::template same<O, X>)
struct operand_arm<G, O, T, P, TypeList<Es...>, Ref<X>> {
  using type =
      TypeList<LeadArm<declared_of_t<X>, Nest<T, spec_ptr<P>, Seq<Es...>>>>;
};
template <class G, class O, class T, auto P, class Rest, class L>
struct operand_arms;
template <class G, class O, class T, auto P, class Rest, class... As>
struct operand_arms<G, O, T, P, Rest, TypeList<As...>>
    : tl_cat<typename operand_arm<G, O, T, P, Rest, As>::type...> {};

template <class G, class O, class E>
struct arm_of {
  using type = TypeList<BaseArm<E>>;
};
template <class G, class O, class X>
  requires(!is_grammar<X> && G::template same<O, X>)
struct arm_of<G, O, Ref<X>> {
  using type = TypeList<LeadArm<declared_of_t<X>, Epsilon>>;
};
template <class G, class O, class X, class... Es>
  requires(!is_grammar<X> && G::template same<O, X>)
struct arm_of<G, O, Seq<Ref<X>, Es...>> {
  using type = TypeList<LeadArm<declared_of_t<X>, Seq<Es...>>>;
};
template <class G, class O, class T, auto P, class X, class... Es>
  requires(!is_grammar<X> && G::template same<O, X> && sizeof...(Es) != 0 &&
           std::is_void_v<spec_narrow_t<P>>)
struct arm_of<G, O, Create<T, Seq<Append<P, Ref<X>>, Es...>>> {
  using type =
      TypeList<LeadArm<declared_of_t<X>, Nest<T, spec_ptr<P>, Seq<Es...>>>>;
};
template <class G, class O, class T, auto P, class W, class... Es>
  requires(sizeof...(Es) != 0 && std::is_void_v<spec_narrow_t<P>> &&
           operand_leads<G, O, typename fold_alts<W>::type>::value)
struct arm_of<G, O, Create<T, Seq<Assign<P, W>, Es...>>>
    : operand_arms<G, O, T, P, TypeList<Es...>, typename fold_alts<W>::type> {};
// An alternative that opens on an optional item: the item taken -- once at
// least, for a repetition -- and the item left out.
template <class E>
struct optional_head : std::false_type {};
template <std::size_t M, class X>
struct optional_head<Repeat<0, M, X>> : std::true_type {
  using taken = cond_t<M == 1, X, Repeat<1, M, X>>;
  using left = Epsilon;
};
template <std::size_t M, class X, class... Es>
struct optional_head<Seq<Repeat<0, M, X>, Es...>> : std::true_type {
  using taken = join_t<cond_t<M == 1, X, Repeat<1, M, X>>, Es...>;
  using left = join_t<Es...>;
};
// A trivia scope around the optional item: the dropped reading leaves it out
// whole, so the next element reads under the caller's scope.
template <class I, class H, std::size_t M, class X, class... Es>
struct optional_head<Seq<Skip<I, H, Repeat<0, M, X>>, Es...>> : std::true_type {
  using taken = join_t<Skip<I, H, cond_t<M == 1, X, Repeat<1, M, X>>>, Es...>;
  using left = join_t<Es...>;
};
template <class T, class E>
  requires(optional_head<E>::value)
struct optional_head<Create<T, E>> : std::true_type {
  using taken = Create<T, typename optional_head<E>::taken>;
  using left = Create<T, typename optional_head<E>::left>;
};

template <class G, class O, class L>
struct any_member;
template <class G, class O, class... Xs>
struct any_member<G, O, TypeList<Xs...>>
    : std::bool_constant<(G::template same<O, Xs> || ...)> {};

// A call of the loop inside or behind optional items is read as the
// alternatives they spell, the item taken before the item left out, as the
// election prefers: `X? A y` is `X A y | A y`. Past the optional gate the
// alternative stays as written, and the descent refuses it.
template <class G, class O, class E, unsigned Left = kOptionalGate,
          bool Spell = Left != 0 && optional_head<E>::value &&
                       any_member<G, O, typename lead_refs<E>::type>::value>
struct spelled {
  using type = TypeList<E>;
};
template <class G, class O, class E, unsigned Left>
struct spelled<G, O, E, Left, true>
    : tl_cat<typename spelled<G, O, typename optional_head<E>::taken,
                              Left - 1>::type,
             typename spelled<G, O, typename optional_head<E>::left,
                              Left - 1>::type> {};

template <class G, class O, class L>
struct arms_of;
template <class G, class O, class... As>
struct arms_of<G, O, TypeList<As...>>
    : tl_cat<typename arm_of<G, O, As>::type...> {};
template <class G, class O, class L>
struct spelled_alts;
template <class G, class O, class... As>
struct spelled_alts<G, O, TypeList<As...>>
    : tl_cat<typename spelled<G, O, As>::type...> {};
template <class S, class G, class O>
using member_arms_t = typename arms_of<
    G, O,
    typename spelled_alts<
        G, O, typename fold_alts<typename S::template body<O>>::type>::type>::
    type;

// The first literal an arm reads after the call that starts it, as far as the
// type says: two turns that start on literals neither of which begins the other
// never meet the same token.
inline constexpr std::size_t kKeyChars = 15;
struct LoopKey {
  bool known = false, ci = false;
  std::uint8_t len = 0;
  std::array<char, kKeyChars> text{};
  // The operators of one infix never share a token: each level has its own.
  const void* infix = nullptr;
  std::uint8_t level = 0;
};
template <class E>
struct recipe_key {
  static constexpr LoopKey value{};
};
template <str Text, bool CI, class Guard>
struct recipe_key<Lit<Text, CI, Guard>> {
  static constexpr LoopKey value = [] {
    LoopKey k;
    k.known = true;
    k.ci = CI;
    const auto v = Text.view();
    k.len =
        static_cast<std::uint8_t>(v.size() < kKeyChars ? v.size() : kKeyChars);
    for (std::size_t i = 0; i < k.len; ++i) k.text[i] = v[i];
    return k;
  }();
};
template <class E, class... Es>
struct recipe_key<Seq<E, Es...>> : recipe_key<E> {};
template <class T, auto P, class E>
struct recipe_key<Nest<T, P, E>> : recipe_key<E> {};
template <class R>
struct recipe_key<Ref<R>> {
  static constexpr LoopKey value = [] {
    if constexpr (!is_grammar<R> && is_terminal_rule<R>)
      return recipe_key<std::remove_cv_t<decltype(R::body)>>::value;
    else
      return LoopKey{};
  }();
};
template <auto Sp, class E>
struct recipe_key<Assign<Sp, E>> : recipe_key<E> {};
template <auto Sp, class E>
struct recipe_key<Append<Sp, E>> : recipe_key<E> {};
template <auto P, class E>
struct recipe_key<Flag<P, E>> : recipe_key<E> {};

template <class I>
inline constexpr char infix_tag = 0;
template <class RR>
consteval void operator_key(LoopKey& k) {
  if constexpr (requires {
                  RR::operator_level;
                  typename RR::operator_infix;
                }) {
    k.infix = &infix_tag<typename RR::operator_infix>;
    k.level = static_cast<std::uint8_t>(RR::operator_level);
  }
}

constexpr bool loop_keys_apart(const LoopKey& a, const LoopKey& b) {
  if (a.infix != nullptr && a.infix == b.infix && a.level != b.level)
    return true;
  if (!a.known || !b.known) return false;
  const bool ci = a.ci || b.ci;
  const std::size_t n = a.len < b.len ? a.len : b.len;
  for (std::size_t i = 0; i < n; ++i) {
    const char x = ci ? to_lower(a.text[i]) : a.text[i];
    const char y = ci ? to_lower(b.text[i]) : b.text[i];
    if (x != y) return true;
  }
  return false;
}

inline constexpr std::size_t kLoopPlaces = 48;
inline constexpr std::size_t kLoopArms = 16;

template <std::size_t N>
struct LoopTable {
  std::array<std::uint8_t, N> arms{};
  std::array<std::array<std::int8_t, kLoopArms>, N> lead{};
  std::array<std::array<std::uint8_t, kLoopArms>, N> unit{};
  std::array<std::array<LoopKey, kLoopArms>, N> key{};
  bool ok = true;
};

template <class T>
using by_place = std::array<std::array<T, kLoopArms>, kLoopPlaces>;

struct LoopPlan {
  bool ok = true;
  std::size_t places = 1, order = 0;
  std::array<std::uint8_t, kLoopPlaces> member{}, arms{}, armUp{};
  std::array<std::int8_t, kLoopPlaces> parent{};
  by_place<std::uint8_t> kind{}, target{};
  by_place<std::uint16_t> rank{};
};

// Walking a rule's arms in the order written: a call to a rule of the
// recursion descends into its arms, in a place of its own; a call to a rule
// the walk is already inside returns to that place.
template <std::size_t N>
constexpr void loop_walk(LoopPlan& pl, const LoopTable<N>& t, std::size_t at) {
  const std::size_t m = pl.member[at];
  pl.arms[at] = t.arms[m];
  for (std::size_t j = 0; j < t.arms[m] && pl.ok; ++j) {
    if (t.lead[m][j] < 0) {
      pl.kind[at][j] = 0;
      pl.rank[at][j] = static_cast<std::uint16_t>(pl.order++);
      continue;
    }
    auto to = static_cast<int>(at);
    while (to >= 0 && pl.member[to] != t.lead[m][j]) to = pl.parent[to];
    if (to >= 0) {
      pl.kind[at][j] = 1;
      pl.target[at][j] = static_cast<std::uint8_t>(to);
      pl.rank[at][j] = static_cast<std::uint16_t>(pl.order++);
      continue;
    }
    if (pl.places == kLoopPlaces) {
      pl.ok = false;
      return;
    }
    const std::size_t c = pl.places++;
    pl.member[c] = static_cast<std::uint8_t>(t.lead[m][j]);
    pl.parent[c] = static_cast<std::int8_t>(at);
    pl.armUp[c] = static_cast<std::uint8_t>(j);
    pl.kind[at][j] = 2;
    pl.target[at][j] = static_cast<std::uint8_t>(c);
    loop_walk(pl, t, c);
  }
}

constexpr bool loop_below(const LoopPlan& pl, std::size_t deep,
                          std::size_t high) {
  for (int p = pl.parent[deep]; p >= 0; p = pl.parent[p])
    if (static_cast<std::size_t>(p) == high) return true;
  return false;
}

// A turn that reads nothing, however far it climbs, reads no text at all.
template <std::size_t N>
constexpr bool loop_reads(const LoopPlan& pl, const LoopTable<N>& t,
                          std::size_t at, std::size_t arm) {
  if (!t.unit[pl.member[at]][arm]) return true;
  for (std::size_t x = at; x != pl.target[at][arm];) {
    const auto up = static_cast<std::size_t>(pl.parent[x]);
    if (!t.unit[pl.member[up]][pl.armUp[x]]) return true;
    x = up;
  }
  return false;
}

// A turn written before another whose target stands below its own is tried
// after it: the nesting is the election's only where the two cannot start on
// the same token.
template <std::size_t N>
constexpr bool loop_nests(const LoopPlan& pl, const LoopTable<N>& t) {
  for (std::size_t a = 0; a < pl.places; ++a)
    for (std::size_t i = 0; i < pl.arms[a]; ++i) {
      if (pl.kind[a][i] != 1) continue;
      for (std::size_t b = 0; b < pl.places; ++b)
        for (std::size_t j = 0; j < pl.arms[b]; ++j) {
          if (pl.kind[b][j] != 1 || pl.rank[a][i] >= pl.rank[b][j]) continue;
          const std::size_t ua = pl.target[a][i], ub = pl.target[b][j];
          if (ua == ub || !loop_below(pl, ub, ua)) continue;
          const std::size_t ma = pl.member[a], mb = pl.member[b];
          LoopKey outer = t.unit[ma][i] ? LoopKey{} : t.key[ma][i];
          for (std::size_t x = ub; x != ua;) {
            const auto up = static_cast<std::size_t>(pl.parent[x]);
            const std::size_t mu = pl.member[up];
            if (!t.unit[mu][pl.armUp[x]]) {
              outer = t.key[mu][pl.armUp[x]];
              break;
            }
            x = up;
          }
          const LoopKey inner = t.unit[mb][j] ? LoopKey{} : t.key[mb][j];
          if (!loop_keys_apart(outer, inner)) return false;
        }
    }
  return true;
}

template <std::size_t N>
constexpr LoopPlan loop_plan(const LoopTable<N>& t, std::size_t head) {
  LoopPlan pl;
  pl.member[0] = static_cast<std::uint8_t>(head);
  pl.parent[0] = -1;
  if (!t.ok) pl.ok = false;
  if (pl.ok) loop_walk(pl, t, 0);
  for (std::size_t p = 0; pl.ok && p < pl.places; ++p)
    for (std::size_t j = 0; j < pl.arms[p]; ++j)
      if (pl.kind[p][j] == 1 && !loop_reads(pl, t, p, j)) pl.ok = false;
  if (pl.ok && !loop_nests(pl, t)) pl.ok = false;
  return pl;
}

// A loop through other rules is read only when some arm is a bare call to the
// next rule: a cycle of wrappers alone has no reading to nest.
template <std::size_t N>
constexpr bool loop_lifts(const LoopTable<N>& t) {
  if constexpr (N == 1) return true;
  for (std::size_t m = 0; m < N; ++m)
    for (std::size_t j = 0; j < t.arms[m]; ++j)
      if (t.lead[m][j] >= 0 && t.lead[m][j] != static_cast<int>(m) &&
          t.unit[m][j])
        return true;
  return false;
}

template <class S, class G, class Members>
struct LoopTables;
template <class S, class G, class... Ms>
struct LoopTables<S, G, TypeList<Ms...>> {
  using members = TypeList<Ms...>;
  static constexpr std::size_t count = sizeof...(Ms);
  template <std::size_t M>
  using arms = member_arms_t<S, G, tl_at_t<M, members>>;

  template <std::size_t M, std::size_t J, class A>
  static consteval void fill_arm(LoopTable<count>& t) {
    if constexpr (requires { typename A::lead; }) {
      t.lead[M][J] =
          static_cast<std::int8_t>(index_of<typename A::lead, members>::value);
      t.unit[M][J] = A::unit;
      t.key[M][J] = recipe_key<typename A::recipe>::value;
      if constexpr (J == 0)
        operator_key<typename S::template rule<tl_at_t<M, members>>>(
            t.key[M][J]);
    } else {
      t.lead[M][J] = -1;
    }
  }
  template <std::size_t M, class L, std::size_t... Js>
  static consteval void fill_arms(LoopTable<count>& t,
                                  std::index_sequence<Js...>) {
    (fill_arm<M, Js, tl_at_t<Js, L>>(t), ...);
  }
  template <std::size_t M>
  static consteval void fill(LoopTable<count>& t) {
    using L = arms<M>;
    constexpr std::size_t n = tl_size<L>::value;
    if constexpr (n > kLoopArms) {
      t.ok = false;
    } else {
      t.arms[M] = static_cast<std::uint8_t>(n);
      fill_arms<M, L>(t, std::make_index_sequence<n>{});
    }
  }
  template <std::size_t... Is>
  static consteval LoopTable<count> build(std::index_sequence<Is...>) {
    LoopTable<count> t;
    if constexpr (count > 100)
      t.ok = false;
    else
      (fill<Is>(t), ...);
    return t;
  }
  static constexpr LoopTable<count> table =
      build(std::make_index_sequence<count>{});
};

template <class S, class G, class Head>
struct LoopFold {
  using members = typename G::template component<Head>;
  using tables = LoopTables<S, G, members>;
  static constexpr LoopPlan plan =
      loop_plan(tables::table, index_of<Head, members>::value);
  static constexpr bool ok = plan.ok && loop_lifts(tables::table);

  template <std::size_t P>
  using declared_at = tl_at_t<plan.member[P], members>;
  template <std::size_t P>
  static constexpr bool converts =
      is_data_rule<typename S::template rule<declared_at<P>>>;
  template <std::size_t P, std::size_t J>
  using arm_at = tl_at_t<J, typename tables::template arms<plan.member[P]>>;

  struct Turn {
    std::size_t at = 0, arm = 0;
  };
  struct TurnList {
    std::array<Turn, kLoopPlaces * kLoopArms> list{};
    std::size_t count = 0;
  };
  static consteval TurnList turns_to(std::size_t p) {
    TurnList out;
    for (std::size_t a = 0; a < plan.places; ++a)
      for (std::size_t j = 0; j < plan.arms[a]; ++j)
        if (plan.kind[a][j] == 1 && plan.target[a][j] == p)
          out.list[out.count++] = {a, j};
    std::sort(out.list.begin(), out.list.begin() + out.count,
              [](const Turn& x, const Turn& y) {
                return plan.rank[x.at][x.arm] < plan.rank[y.at][y.arm];
              });
    return out;
  }
};

template <class F, std::size_t P>
struct reading;

// What an arm does to the reading it receives, closed over the rule that owns
// it when that rule converts text, unless the group of the place it lifts into
// closes it.
template <class F, std::size_t Q, std::size_t Within, class E>
using applied_t =
    cond_t<Q == Within || !F::template converts<Q>, E,
           Closed<typename F::template declared_at<Q>, E, true, false>>;

template <class F, std::size_t P, std::size_t J, int Kind = F::plan.kind[P][J]>
struct alt_for {
  using type = TypeList<>;
};
template <class F, std::size_t P, std::size_t J>
struct alt_for<F, P, J, 0> {
  using type = TypeList<typename F::template arm_at<P, J>::expr>;
};
// A place that only turns back reads nothing by itself: no alternative leads
// into it.
template <class F, std::size_t C, class Recipe,
          bool Reads = reading<F, C>::reads>
struct descent_alt {
  using type = TypeList<>;
};
template <class F, std::size_t C, class Recipe>
struct descent_alt<F, C, Recipe, true> {
  using type = TypeList<join_t<typename reading<F, C>::type, Recipe>>;
};
template <class F, std::size_t P, std::size_t J>
struct alt_for<F, P, J, 2>
    : descent_alt<F, F::plan.target[P][J],
                  typename F::template arm_at<P, J>::recipe> {};

template <class F, std::size_t P, class Js>
struct alts_of;
template <class F, std::size_t P, std::size_t... Js>
struct alts_of<F, P, std::index_sequence<Js...>>
    : tl_cat<typename alt_for<F, P, Js>::type...> {};

template <class F, std::size_t P>
struct loop_of;

template <class F, std::size_t X, std::size_t P, bool Here = (X == P)>
struct lifted {
  using type = Epsilon;
};
template <class F, std::size_t X, std::size_t P>
struct lifted<F, X, P, false> {
  static constexpr std::size_t up = F::plan.parent[X];
  using type = join_t<
      typename loop_of<F, X>::type,
      applied_t<F, up, P,
                typename F::template arm_at<up, F::plan.armUp[X]>::recipe>,
      typename lifted<F, up, P>::type>;
};

template <class F, std::size_t P, std::size_t At, std::size_t J>
struct excursion {
  using type =
      join_t<applied_t<F, At, P, typename F::template arm_at<At, J>::recipe>,
             typename lifted<F, At, P>::type>;
};

template <class F, std::size_t P, class Is>
struct excursions;
template <class F, std::size_t P, std::size_t... Is>
struct excursions<F, P, std::index_sequence<Is...>> {
  static constexpr auto turns = F::turns_to(P);
  using type = TypeList<
      typename excursion<F, P, turns.list[Is].at, turns.list[Is].arm>::type...>;
};

template <class F, std::size_t P>
struct loop_of {
  static constexpr std::size_t count = F::turns_to(P).count;
  using list = typename excursions<F, P, std::make_index_sequence<count>>::type;
  using type =
      cond_t<count == 0, Epsilon,
             Turns<Repeat<0, kUnbounded,
                          Closed<typename F::template declared_at<P>,
                                 typename or_of<list>::type, true, P == 0>>>>;
};

template <class F, std::size_t P>
struct reading {
  using alts =
      typename alts_of<F, P, std::make_index_sequence<F::plan.arms[P]>>::type;
  static constexpr bool reads = tl_size<alts>::value != 0;
  using group = Closed<typename F::template declared_at<P>,
                       typename or_of<alts>::type, false, true>;
  using type = join_t<group, typename loop_of<F, P>::type>;
};

// The rules a body calls once it has consumed a token.
template <class E, class Kids = typename ExprShape<E>::kids>
struct late_refs;
template <class E, class... Ks>
struct late_refs<E, TypeList<Ks...>> : tl_cat<typename late_refs<Ks>::type...> {
};
// What follows the first item is read once it has consumed a token: an
// optional item consumes one wherever it is taken.
template <class E, class... Es>
struct late_refs<Seq<E, Es...>, TypeList<E, Es...>> {
  using type = tl_cat_t<typename late_refs<E>::type, refs_t<Es>...>;
};
template <class G, class O, class M, class C>
struct late_refs<Embed<G, Seq<O, M, C>>, TypeList<Seq<O, M, C>>>
    : refs<Embed<G, Seq<O, M, C>>> {};

template <class S, class X,
          bool Terminal = is_terminal_rule<typename S::template rule<X>>>
struct body_refs {
  using all = TypeList<>;
  using late = TypeList<>;
};
template <class S, class X>
struct body_refs<S, X, false> {
  using all = typename as_declared<refs_t<typename S::template body<X>>>::type;
  using late = typename as_declared<
      typename late_refs<typename S::template body<X>>::type>::type;
};

// The rules a call can start reading at: the roots, those a rule outside a
// loop names, and those a rule of the loop names after a token.
template <class S, class G, class Rules = typename G::rules>
struct CallTable;
template <class S, class G, class... Rs>
struct CallTable<S, G, TypeList<Rs...>> {
  static constexpr std::size_t n = sizeof...(Rs);
  static constexpr std::size_t words = (n + 63) / 64;
  using Mask = std::array<std::uint64_t, words>;
  template <class T>
  static consteval void set(Mask& m) {
    constexpr std::size_t at = G::template at<T>;
    if constexpr (at < n) m[at / 64] |= std::uint64_t{1} << (at % 64);
  }
  template <class... Ts>
  static consteval Mask mask(TypeList<Ts...>) {
    Mask m{};
    (set<Ts>(m), ...);
    return m;
  }
  static constexpr std::array<Mask, n> all = {
      mask(typename body_refs<S, Rs>::all{})...};
  static constexpr std::array<Mask, n> late = {
      mask(typename body_refs<S, Rs>::late{})...};
  static constexpr Mask roots = mask(typename cursiv::detail::roots<S>::type{});

  static consteval Mask reached(std::uint32_t loop) {
    Mask m = roots;
    for (std::size_t i = 0; i < n; ++i) {
      const Mask& from = G::strong.component[i] == loop ? late[i] : all[i];
      for (std::size_t w = 0; w < words; ++w) m[w] |= from[w];
    }
    return m;
  }
  template <std::uint32_t Loop>
  static constexpr Mask reach = reached(Loop);

  template <class M>
  static consteval bool headed() {
    constexpr std::size_t at = G::template at<M>;
    return (reach<G::strong.component[at]>[at / 64] >> (at % 64)) & 1u;
  }
};

// A loop is read only when every rule a call can start at has a fold: one
// head left unread would be read as another's.
template <class S, class G, class Members>
struct heads_fold;
template <class S, class G, class... Ms>
struct heads_fold<S, G, TypeList<Ms...>>
    : std::bool_constant<(
          (!CallTable<S, G>::template headed<Ms>() || LoopFold<S, G, Ms>::ok) &&
          ...)> {};

template <class S, class G, class R, bool Cyclic = G::template cyclic<R>>
struct loop_fold_ok : std::false_type {};
template <class S, class G, class R>
struct loop_fold_ok<S, G, R, true>
    : std::bool_constant<
          LoopFold<S, G, declared_of_t<R>>::ok &&
          heads_fold<S, G, typename G::template component<R>>::value> {};

template <class S, class G, class R, bool Fold = loop_fold_ok<S, G, R>::value>
struct loop_fold_rule {
  using type = typename S::template rule<R>;
};
template <class S, class G, class R>
struct loop_fold_rule<S, G, R, true> {
  using RR = typename S::template rule<R>;
  struct type : RR {
    using declared = RR;
    static constexpr auto body =
        typename reading<LoopFold<S, G, declared_of_t<R>>, 0>::type{};
    static constexpr bool minted_body = true;
    static constexpr bool ll_folded = true;
    using fold_members = typename LoopFold<S, G, declared_of_t<R>>::members;
  };
};

}  // namespace cursiv::detail::llow
