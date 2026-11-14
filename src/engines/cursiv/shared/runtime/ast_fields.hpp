// What an AST member is, and what a payload may write into it. It never
// refuses: the generator reports refusals from these answers, and the
// builder's thunks read the same ones.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// The AST classes a field points at. A base nothing ever builds is
// registered exactly this way.
template <class X>
struct held_targets {
  using type = TypeList<>;
};
template <class... Ts>
struct held_targets<cursiv::reference<Ts...>> {
  using type = TypeList<Ts...>;
};
template <class... Ts>
struct held_targets<cursiv::multi_reference<Ts...>> {
  using type = TypeList<Ts...>;
};
template <class X>
  requires(std::is_pointer_v<X> && std::is_class_v<std::remove_pointer_t<X>>)
struct held_targets<X> {
  using type = TypeList<std::remove_pointer_t<X>>;
};
template <class... As>
struct held_targets<std::variant<As...>>
    : tl_cat<typename held_targets<As>::type...> {};

template <class X>
struct variant_alts {
  using type = TypeList<>;
};
template <class... As>
struct variant_alts<std::variant<As...>> {
  using type = TypeList<As...>;
};

template <class X, class Src>
consteval bool admits();

// The converter's result type, or the type of the matched text when there is
// none. A node payload produces no value, so nothing matches it exactly.
template <class Src>
struct payload_value {
  using type = void;
};
template <class R>
struct payload_value<text_payload<R>> {
  using type = std::remove_cvref_t<typename text_value<R>::type>;
};

// The alternatives one payload may write, in declaration order: those whose
// type is the payload's value, or else those constructible from it, which
// must be exactly one. The writer emplaces the head.
template <class Src, class L>
struct variant_cands;
template <class Src, class... As>
struct variant_cands<Src, TypeList<As...>> {
 private:
  template <class A>
  static constexpr bool exact_alt =
      std::is_same_v<std::remove_cvref_t<A>,
                     typename payload_value<Src>::type> &&
      admits<A, Src>();
  static constexpr int exact_n = (0 + ... + (exact_alt<As> ? 1 : 0));

 public:
  using type =
      tl_cat_t<cond_t<(exact_n != 0 ? exact_alt<As> : admits<As, Src>()),
                      TypeList<As>, TypeList<>>...>;
  static constexpr int count = static_cast<int>(tl_size<type>::value);
};

// What a reference's name may come from: a `std::string` a converter built is
// kept by the tree.
template <class V>
inline constexpr bool is_text_value =
    std::is_same_v<V, std::string_view> || std::is_same_v<V, std::string>;

// A `string_view` member takes any text the tree can name: the matched text,
// a converter's view of it, or a data rule's parts. A converter's `std::string`
// needs a `string` member.
template <class R>
inline constexpr bool source_spells = [] {
  if constexpr (std::is_void_v<R>)
    return true;
  else
    return std::is_same_v<std::remove_cvref_t<typename text_value<R>::type>,
                          std::string_view>;
}();

// A list or an optional a converter may answer whole: of values only, since a
// node or a reference is the tree's to fill.
template <class F>
consteval bool takes_whole() {
  if constexpr (F::storage == FieldStorage::direct)
    return false;
  else if constexpr (F::content == FieldContent::value)
    return true;
  else if constexpr (F::content == FieldContent::variant)
    return tl_size<typename variant_node_alt<typename F::held>::type>::value ==
           0;
  else
    return false;
}
// A payload answering the very type its member has: `=` writes it whole.
template <class F, class Src>
inline constexpr bool writes_whole =
    takes_whole<F>() &&
    std::is_same_v<typename payload_value<Src>::type, typename F::member>;

// A number the member would cut short. An integer keeps its size in a floating
// member, which is what a number literal of either form asks of one.
template <class X, class V>
inline constexpr bool narrows = [] {
  if constexpr (std::is_enum_v<V>) {
    return narrows<X, std::underlying_type_t<V>>;
  } else if constexpr (std::is_arithmetic_v<X> && std::is_arithmetic_v<V>) {
    using To = std::numeric_limits<X>;
    using From = std::numeric_limits<V>;
    if (!To::is_integer && From::is_integer) return false;
    return From::digits > To::digits || (From::is_signed && !To::is_signed) ||
           (To::is_integer && !From::is_integer);
  } else {
    return false;
  }
}();

// Whether member type `X` takes source `Src`: a reference takes text, a node
// slot a node of its class, a variant one that exactly one alternative takes,
// and anything else text convertible to it.
template <class X, class Src>
consteval bool admits() {
  if constexpr (is_reference_v<X> || is_multi_reference_v<X>) {
    // Text and nothing else. A view of the converter's own local is not caught,
    // and cannot be.
    return is_text_payload<Src> &&
           is_text_value<typename payload_value<Src>::type>;
  } else if constexpr (is_variant_v<X>) {
    return variant_cands<Src, typename variant_alts<X>::type>::count == 1 ||
           (std::is_same_v<typename payload_value<Src>::type, X> &&
            std::is_move_constructible_v<X> &&
            tl_size<typename variant_node_alt<X>::type>::value == 0);
  } else if constexpr (content_of<X> == FieldContent::node) {
    // The pointee must derive from `AstNode`: the writer narrows an `AstNode*`
    // to it and sets `parent` through it.
    if constexpr (is_node_payload<Src>)
      return std::is_base_of_v<AstNode, std::remove_pointer_t<X>> &&
             std::is_base_of_v<std::remove_pointer_t<X>, typename Src::type>;
    else
      return false;
  } else {
    if constexpr (is_text_payload<Src>) {
      using V = typename text_value<typename Src::rule>::type;
      if constexpr (std::is_same_v<X, AstNode::string_view>)
        return source_spells<typename Src::rule>;
      else if constexpr (text_value<typename Src::rule>::converted ||
                         std::is_same_v<std::remove_cvref_t<V>, std::string>)
        // A value, a data rule's passed on as much as a converter's.
        // Constructible alone: the builder constructs, and for every non-array
        // type convertibility implies constructibility.
        return std::is_constructible_v<X, V> &&
               !narrows<X, std::remove_cvref_t<V>>;
      else if constexpr (std::is_same_v<X, AstNode::string>)
        return source_spells<typename Src::rule>;
      else
        // With no converter, the member converts the matched text itself, or
        // its type's own converter does.
        return has_default_convert<X> || written_converter<X>();
    } else {
      return false;
    }
  }
}

// The first hit's index, `kNoBranch` for none.
constexpr std::uint8_t first_branch(std::initializer_list<bool> hits) noexcept {
  std::uint8_t i = 0;
  for (const bool hit : hits) {
    if (hit) return i;
    ++i;
  }
  return kNoBranch;
}

// The candidates N derives from: how many, and the rank of the first -- a site
// narrowing to two is refused before the rank is asked.
template <class N, class L>
struct narrow_bases;
template <class N, class... Ts>
struct narrow_bases<N, TypeList<Ts...>> {
  static constexpr std::size_t count =
      (0u + ... + (std::is_base_of_v<Ts, N> ? 1u : 0u));
  static constexpr std::uint8_t rank =
      first_branch({std::is_base_of_v<Ts, N>...});
};

}  // namespace cursiv::engine
