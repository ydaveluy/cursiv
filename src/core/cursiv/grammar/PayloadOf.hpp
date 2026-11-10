// What a grammar expression produces for the directive above it: a node, or
// the text a rule denotes. An expression trait, asked to name what a directive
// reads and which conversion it runs.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/ctstring.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::detail {

// One source per alternative: a choice payload has several, and every one of
// them must be able to write the member.

// The payload builds a T.
template <class T>
struct node_payload {
  using type = T;
};
// R is the value rule that matched the text, or void for a bare matcher.
template <class R>
struct text_payload {
  using rule = R;
};

template <class S>
inline constexpr bool is_text_payload = false;
template <class R>
inline constexpr bool is_text_payload<text_payload<R>> = true;
template <class S>
inline constexpr bool is_node_payload = false;
template <class T>
inline constexpr bool is_node_payload<node_payload<T>> = true;

template <class E>
struct payload_of {
  using type = TypeList<>;
};
template <str S, bool CI, class Gd>
struct payload_of<Lit<S, CI, Gd>> {
  using type = TypeList<text_payload<void>>;
};
template <std::uint64_t M, bool N, str S>
struct payload_of<CpSet<M, N, S>> {
  using type = TypeList<text_payload<void>>;
};
template <>
struct payload_of<Byte> {
  using type = TypeList<text_payload<void>>;
};
template <>
struct payload_of<Indent> {
  using type = TypeList<text_payload<void>>;
};
template <>
struct payload_of<Dedent> {
  using type = TypeList<text_payload<void>>;
};
template <class T, class C>
struct payload_of<Implied<T, C>> {
  using type = TypeList<text_payload<void>>;
};
template <class D>
struct payload_of<SuperT<D>> : super_escaped<SuperT<D>> {};
// A run up to a closer matches text, opener and closer included.
template <class O, class Cl>
struct payload_of<Through<O, Cl>> {
  using type = TypeList<text_payload<void>>;
};
template <class Tag>
struct payload_of<Same<Tag>> {
  using type = TypeList<text_payload<void>>;
};
// A node rule (an infix rule too: its climb builds nodes) hands over a node;
// a terminal or data rule hands over the text it matched, converted.
template <class R0>
struct payload_of<Ref<R0>> {
  using R = named_rule_t<R0>;
  using type = TypeList<cond_t<is_node_rule<R> || is_infix_rule<R>,
                               node_payload<rule_type_t<R>>, text_payload<R>>>;
};
template <class T, class E>
struct payload_of<Create<T, E>> {
  using type = TypeList<node_payload<T>>;
};
template <class... As>
struct payload_of<Choice<As...>> : tl_cat<typename payload_of<As>::type...> {};
// The conditions around the one element that reads leave it the value.
template <class... Es>
  requires(tl_size<readers_t<Es...>>::value == 1)
struct payload_of<Seq<Es...>> : payload_of<tl_at_t<0, readers_t<Es...>>> {};
// Doc text, a mark and a region give what they wrap.
template <class E>
  requires transparent<E>
struct payload_of<E> : payload_of<through_t<E>> {};

// A bare literal's payload is its own spelling, so whether the member's
// conversion can accept it is decided here. A list only validation reads,
// shaped like `payload_of`'s, which erases the spelling.
template <str S>
struct lit_payload {
  static constexpr auto spelling = S;
};

template <class E>
struct lit_payloads {
  using type = TypeList<>;
};
template <str S, bool CI, class Gd>
struct lit_payloads<Lit<S, CI, Gd>> {
  // A case-insensitive literal does not match its own spelling alone, so the
  // spelling is not what the member receives and nothing is decidable.
  using type = cond_t<CI, TypeList<>, TypeList<lit_payload<S>>>;
};
template <class... As>
struct lit_payloads<Choice<As...>>
    : tl_cat<typename lit_payloads<As>::type...> {};
template <class... Es>
  requires(tl_size<readers_t<Es...>>::value == 1)
struct lit_payloads<Seq<Es...>> : lit_payloads<tl_at_t<0, readers_t<Es...>>> {};
template <class E>
  requires transparent<E>
struct lit_payloads<E> : lit_payloads<through_t<E>> {};

template <class... Ps>
consteval auto joined_spelling(TypeList<Ps...>) {
  char text[(1 + ... + Ps::spelling.size())]{};
  std::size_t at = 0;
  ((std::ranges::copy(Ps::spelling.view(), text + at),
    at += Ps::spelling.size()),
   ...);
  return str<(1 + ... + Ps::spelling.size())>(text);
}
// An inline rule of literals only gives their spellings joined, as a data
// rule's text.
template <class E>
struct lit_joined {
  using type = TypeList<>;
};
template <class... Es>
  requires((!is_condition<Es> || ...) &&
           ((is_condition<Es> ||
             tl_size<typename lit_payloads<Es>::type>::value == 1) &&
            ...))
struct lit_joined<Seq<Es...>> {
  using parts = tl_cat_t<typename lit_payloads<
      std::conditional_t<is_condition<Es>, Epsilon, Es>>::type...>;
  using type = TypeList<lit_payload<joined_spelling(parts{})>>;
};
template <class E>
  requires is_wrapper<E>
struct lit_joined<E> : lit_joined<through_t<E>> {};
template <class R>
  requires(is_anonymous_rule<R> && !is_terminal_rule<R> && slot_empty<R>)
struct lit_payloads<Ref<R>> : lit_joined<std::remove_cv_t<decltype(R::body)>> {
};

// What a text payload carries: the matched text, converted by the member's
// type, or the converter's result. The converter is the resolved rule's, as
// `rule_type` reads it.
template <class R>
struct converter_rule {
  static consteval auto pick() {
    if constexpr (HasConverter<conv_t<R>>)
      return std::type_identity<conv_t<R>>{};
    else if constexpr (is_overridden<R> && slot_empty<R>)
      return std::type_identity<
          typename converter_rule<inherited_of_t<R>>::type>{};
    else if constexpr (is_data_rule<R> && slot_empty<R> &&
                       !std::is_same_v<rule_type_t<R>, std::string_view> &&
                       !std::is_same_v<rule_type_t<R>, std::string>)
      return std::type_identity<pass_conv<rule_type_t<R>>>{};
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

// When two payload sources are one conversion, for `one_conv` and `conv_slot`.
// A rule is erased only where it adds nothing: no converter, no annotation,
// not a data rule.
template <class Src>
struct erased_src {
  using type = Src;
};
// Already erased: it names no rule, so none of the questions can be asked.
template <>
struct erased_src<text_payload<void>> {
  using type = text_payload<void>;
};
template <class R>
struct erased_src<text_payload<R>> {
  static constexpr bool erasable =
      std::is_void_v<converter_rule_t<R>> && !is_data_rule<R> &&
      std::is_same_v<rule_type_t<R>, std::string_view>;
  using type = cond_t<erasable, text_payload<void>, text_payload<R>>;
};
template <class Src>
using erased_src_t = typename erased_src<Src>::type;

}  // namespace cursiv::detail
