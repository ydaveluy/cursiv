// What an AST member is, and what a payload may write into it. It answers,
// it never refuses: the generator states its refusals against this, and
// the builder's thunks decode the same answers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <variant>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

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
struct pay_value {
  using type = void;
};
template <class R>
struct pay_value<pay_text<R>> {
  using type = std::remove_cvref_t<typename text_value<R>::type>;
};

// The alternatives one payload may write, in declaration order, in two rounds
// where the first that answers wins: (1) an alternative whose type is the
// value the payload produces, or `variant<boolean, float64, ...>` would be
// ambiguous, each constructible from the other; (2) constructibility, for
// payloads no alternative names exactly, which must leave exactly one. The
// validation counts this list and the writer emplaces its head.
template <class Src, class L>
struct variant_cands;
template <class Src, class... As>
struct variant_cands<Src, TypeList<As...>> {
 private:
  template <class A>
  static constexpr bool exact_alt =
      std::is_same_v<std::remove_cvref_t<A>, typename pay_value<Src>::type> &&
      admits<A, Src>();
  static constexpr int exact_n = (0 + ... + (exact_alt<As> ? 1 : 0));

 public:
  using type =
      tl_cat_t<cond_t<(exact_n != 0 ? exact_alt<As> : admits<As, Src>()),
                      TypeList<As>, TypeList<>>...>;
  static constexpr int count = static_cast<int>(tl_size<type>::value);
};

// No AST member owns its text: the tree borrows the document, and a
// materialised value is allocated by the converter in the builder's resource.
// A pmr string owns its bytes as a plain one does.
template <class V>
inline constexpr bool is_owned_text =
    std::is_same_v<V, std::string> || std::is_same_v<V, std::pmr::string>;

// X is the member's held type (storage already stripped), Src one payload
// source: references take text whose value type is `string_view` and nothing
// else; `T*` takes a node deriving from `T`; a variant takes it when exactly
// one alternative accepts it, an exact match winning outright; a bare
// `string_view` takes text the builder does not materialise; an owned string
// takes nothing; everything else takes text convertible to the member's type.
template <class X, class Src>
consteval bool admits() {
  if constexpr (is_owned_text<X>) {
    return false;
  } else if constexpr (is_reference_v<X> || is_multi_reference_v<X>) {
    // Exact type, not convertibility: a `string_view` is constructible from a
    // `std::string`, and a converter returning an owned temporary would leave
    // the name pointing at freed bytes. A data rule's joined parts live in the
    // arena, so an assembled name passes. A view of the converter's own local
    // is not caught, and cannot be.
    return is_text_pay<Src> &&
           std::is_same_v<typename pay_value<Src>::type, AstNode::string_view>;
  } else if constexpr (is_variant_v<X>) {
    return variant_cands<Src, typename variant_alts<X>::type>::count == 1;
  } else if constexpr (nature_of<X> == FieldNature::node) {
    // The pointee owes the base too: the writer narrows an `AstNode*` to it and
    // wires `parent` through it.
    if constexpr (is_node_pay<Src>)
      return std::is_base_of_v<AstNode, std::remove_pointer_t<X>> &&
             std::is_base_of_v<std::remove_pointer_t<X>, typename Src::type>;
    else
      return false;
  } else {
    if constexpr (is_text_pay<Src>) {
      using V = typename text_value<typename Src::rule>::type;
      // `is_constructible_v<string_view, std::string>` holds, so a converter
      // answering an owned string would leave the member pointing at bytes
      // freed with the `ConversionResult`: read the effective return type, the
      // converter's or the matched text.
      if constexpr (std::is_same_v<X, AstNode::string_view>)
        return !text_value<typename Src::rule>::converted ||
               std::is_same_v<typename pay_value<Src>::type,
                              AstNode::string_view>;
      else if constexpr (text_value<typename Src::rule>::converted)
        // Constructible alone: the builder constructs, and for every non-array
        // type convertibility implies constructibility.
        return std::is_constructible_v<X, V>;
      else
        // Without a converter the matched text reaches the member, whatever the
        // rule is annotated with: a value member is vocabulary or
        // converter-written.
        return has_default_convert<X>;
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

}  // namespace cursiv::detail
