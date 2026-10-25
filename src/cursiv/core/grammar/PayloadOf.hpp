// What a grammar expression produces for the directive above it: a node, or
// the text a rule denotes. An expression trait, asked to name what a directive
// reads and which conversion it runs.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

// One source per alternative: a choice payload has several, and every one of
// them must be able to write the member.

// The payload builds a T.
template <class T>
struct pay_node {
  using type = T;
};
// R is the value rule that matched the text, or void for a bare matcher.
template <class R>
struct pay_text {
  using rule = R;
};

template <class S>
inline constexpr bool is_text_pay = false;
template <class R>
inline constexpr bool is_text_pay<pay_text<R>> = true;
template <class S>
inline constexpr bool is_node_pay = false;
template <class T>
inline constexpr bool is_node_pay<pay_node<T>> = true;

template <class E>
struct payload_of {
  using type = TypeList<>;
};
template <str S, bool CI, class Gd>
struct payload_of<Lit<S, CI, Gd>> {
  using type = TypeList<pay_text<void>>;
};
template <std::uint64_t M, bool N, str S>
struct payload_of<CpSet<M, N, S>> {
  using type = TypeList<pay_text<void>>;
};
template <>
struct payload_of<Byte> {
  using type = TypeList<pay_text<void>>;
};
template <>
struct payload_of<Indent> {
  using type = TypeList<pay_text<void>>;
};
template <>
struct payload_of<Dedent> {
  using type = TypeList<pay_text<void>>;
};
template <class T, class C>
struct payload_of<Implied<T, C>> {
  using type = TypeList<pay_text<void>>;
};
template <class D>
struct payload_of<SuperT<D>> : super_escaped<SuperT<D>> {};
// A run up to a closer matches text, opener and closer included.
template <class O, class Cl>
struct payload_of<Through<O, Cl>> {
  using type = TypeList<pay_text<void>>;
};
template <class Tag>
struct payload_of<Same<Tag>> {
  using type = TypeList<pay_text<void>>;
};
// A node rule (an infix rule too: its climb builds nodes) hands over a node;
// a terminal or data rule hands over the text it matched, converted.
template <class R0>
struct payload_of<Ref<R0>> {
  using R = named_rule_t<R0>;
  using type = TypeList<cond_t<is_node_rule<R> || is_infix_rule<R>,
                               pay_node<rule_type_t<R>>, pay_text<R>>>;
};
template <class T, class E>
struct payload_of<Create<T, E>> {
  using type = TypeList<pay_node<T>>;
};
template <class... As>
struct payload_of<Choice<As...>> : tl_cat<typename payload_of<As>::type...> {};
// The conditions around the one element that reads leave it the value.
template <class... Es>
  requires(tl_size<readers_t<Es...>>::value == 1)
struct payload_of<Seq<Es...>> : payload_of<tl_at_t<0, readers_t<Es...>>> {};
// Prose, a mark and a region pay what they wrap.
template <class E>
  requires transparent<E>
struct payload_of<E> : payload_of<through_t<E>> {};

// A bare literal's payload is its own spelling, so whether the member's
// conversion can ever accept it is decidable here rather than on every match.
//
// A second walk and not a second source: `payload_of` erases the spelling, and
// must keep doing so, because the source type is the identity the conversion
// table and the builder's writer key on. The spelling rides a list only
// validation reads, shaped exactly like `payload_of`'s -- so a spreading choice
// is judged one branch at a time, and what produces no payload produces no
// spelling.
template <str S>
struct pay_lit {
  static constexpr auto spelling = S;
};

template <class E>
struct lit_pays {
  using type = TypeList<>;
};
template <str S, bool CI, class Gd>
struct lit_pays<Lit<S, CI, Gd>> {
  // A case-insensitive literal does not match its own spelling alone, so the
  // spelling is not what the member receives and nothing is decidable.
  using type = cond_t<CI, TypeList<>, TypeList<pay_lit<S>>>;
};
template <class... As>
struct lit_pays<Choice<As...>> : tl_cat<typename lit_pays<As>::type...> {};
template <class... Es>
  requires(tl_size<readers_t<Es...>>::value == 1)
struct lit_pays<Seq<Es...>> : lit_pays<tl_at_t<0, readers_t<Es...>>> {};
template <class E>
  requires transparent<E>
struct lit_pays<E> : lit_pays<through_t<E>> {};

template <class... Ps>
consteval auto joined_spelling(TypeList<Ps...>) {
  char text[(1 + ... + Ps::spelling.size())]{};
  std::size_t at = 0;
  ((std::ranges::copy(Ps::spelling.view(), text + at),
    at += Ps::spelling.size()),
   ...);
  return str<(1 + ... + Ps::spelling.size())>(text);
}
// An inline rule of literals alone pays their spelling joined, as a data
// rule's text.
template <class E>
struct lit_joined {
  using type = TypeList<>;
};
template <class... Es>
  requires(
      (!is_condition<Es> || ...) &&
      ((is_condition<Es> || tl_size<typename lit_pays<Es>::type>::value == 1) &&
       ...))
struct lit_joined<Seq<Es...>> {
  using parts = tl_cat_t<typename lit_pays<
      std::conditional_t<is_condition<Es>, Epsilon, Es>>::type...>;
  using type = TypeList<pay_lit<joined_spelling(parts{})>>;
};
template <class E>
  requires is_wrapper<E>
struct lit_joined<E> : lit_joined<through_t<E>> {};
template <class R>
  requires(is_anonymous_rule<R> && !is_terminal_rule<R> && slot_empty<R>)
struct lit_pays<Ref<R>> : lit_joined<std::remove_cv_t<decltype(R::body)>> {};

// What a text payload carries: without a converter the value is the matched
// text and the member's type decides the conversion; with one, the converter's
// result type reaches the member. No unwrapping here -- `rule_type_t` already
// peeled `ConversionResult` for every reader at once.
//
// The converter is taken from the resolved rule, and read as `rule_type` reads
// the slot: an override that replaces only a body keeps the converter of the
// rule it stood over, one that names a bare target replaces it with that
// target's own conversion.
template <class R>
struct converter_rule {
  static consteval auto pick() {
    if constexpr (HasConverter<conv_t<R>>)
      return std::type_identity<conv_t<R>>{};
    else if constexpr (is_overridden<R> && slot_empty<R>)
      return std::type_identity<
          typename converter_rule<inherited_of_t<R>>::type>{};
    else
      return std::type_identity<void>{};
  }
  using type = typename decltype(pick())::type;
};
template <>
struct converter_rule<void> {
  using type = void;
};
template <class R>
using converter_rule_t = typename converter_rule<R>::type;

template <class R>
struct text_value {
  using type = rule_type_t<R>;
  static constexpr bool converted = !std::is_void_v<converter_rule_t<R>>;
};
template <>
struct text_value<void> {
  using type = std::string_view;
  static constexpr bool converted = false;
};

// When two payload sources are one conversion: stated once, and read by the
// hub's `one_conv` on the declared body and by the fold's `conv_slot` on the
// compiled one. Two relations for one law would drift apart.
//
// Erasing the rule is sound only where it is invisible: `admits` reads the
// value a source produces and whether a converter produced it, so a rule
// answering `string_view` with no converter is indistinguishable from the
// bare-matcher source. An annotated rule is not -- its annotation is what the
// exactness round counted -- and neither is a data rule, whose value is
// composed from its parts.
template <class Src>
struct erased_src {
  using type = Src;
};
// Already erased: it names no rule, so none of the questions can be asked.
template <>
struct erased_src<pay_text<void>> {
  using type = pay_text<void>;
};
template <class R>
struct erased_src<pay_text<R>> {
  static constexpr bool erasable =
      std::is_void_v<converter_rule_t<R>> && !is_data_rule<R> &&
      std::is_same_v<rule_type_t<R>, std::string_view>;
  using type = cond_t<erasable, pay_text<void>, pay_text<R>>;
};
template <class Src>
using erased_src_t = typename erased_src<Src>::type;

}  // namespace cursiv::detail
