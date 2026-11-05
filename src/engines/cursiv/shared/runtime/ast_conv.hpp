// How a converted payload lands in a member: the type it converts to, the
// alternative a variant takes, the converter a site runs. The generator's
// registries and the builder's thunks read one answer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/shared/runtime/ast_fields.hpp"

namespace cursiv::detail {

// What a conversion writes: the member itself, where `place` puts the value, or
// the text a reference binds.
template <class F>
using conv_target_t =
    cond_t<is_ref_nature(F::nature), AstNode::string_view, typename F::member>;

// Whether that rule's payload is one the parts must be joined for. A bare
// matcher names no rule, and `is_data_rule` has none to ask.
template <class R>
inline constexpr bool joins_parts = [] {
  if constexpr (std::is_void_v<R>)
    return false;
  else
    return is_data_rule<R>;
}();

// A converter answering a positioned result carries its own error channel; a
// converter answering a bare value cannot fail.
template <class T>
inline constexpr bool is_conversion_result = false;
template <class T>
inline constexpr bool is_conversion_result<ConversionResult<T>> = true;

// Where the converted value lands: a variant takes the one alternative this
// payload writes. The same `variant_cands` the validation counts, so "checked
// against this alternative" and "emplaces it" are one statement.
template <class M, class Src, bool = is_variant_v<M>>
struct conv_alt {
  using type = M;
};
template <class L>
struct first_alt {
  using type = void;
};
template <class A, class... As>
struct first_alt<TypeList<A, As...>> {
  using type = A;
};
template <class M, class Src>
struct conv_alt<M, Src, true>
    : first_alt<
          typename variant_cands<Src, typename variant_alts<M>::type>::type> {};

template <class M, std::size_t Alt, bool = is_variant_v<M>>
struct conv_alt_at {
  using type = M;
};
template <class M, std::size_t Alt>
struct conv_alt_at<M, Alt, true> {
  using type = tl_at_t<Alt, typename variant_alts<M>::type>;
};
template <class M, class Src>
consteval std::size_t conv_alt_rank() {
  using H = typename strip_storage<M>::type;
  if constexpr (is_variant_v<H> &&
                !std::is_void_v<typename conv_alt<H, Src>::type>)
    return index_of<typename conv_alt<H, Src>::type,
                    typename variant_alts<H>::type>::value;
  else
    return 0;
}

// The converter one site runs, from the declarations an author wrote: the
// override whose slot decides it (`slot_decl_t`) or, where no override fills
// one, the declared rule's. The rule a stage minted over them has no spelling.
template <class Declared, class Over>
struct site_conv {
  using type = conv_t<Over>;
};
template <class Declared>
struct site_conv<Declared, void> {
  using type = converter_rule_t<Declared>;
};
template <class Declared, class Over>
using site_conv_t = typename site_conv<Declared, Over>::type;

template <class CR, class M>
inline constexpr M kConvValue = CR::convert();

// The list is `variant_node_alt`'s, in the order the field's `targets` were
// folded in, so `alt` indexes both.
template <class L, class M>
bool emplace_node_alt(std::uint16_t alt, M& member, AstNode* value) {
  return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    bool done = false;
    ((Is == alt ? (void)(place<tl_at_t<Is, L>>(
                             member,
                             ast_cast_unchecked<
                                 std::remove_pointer_t<tl_at_t<Is, L>>>(value)),
                         done = true)
                : (void)0),
     ...);
    return done;
  }(std::make_index_sequence<tl_size<L>::value>{});
}

// The node a single-valued member holds; a list only grows.
template <class M>
CURSIV_ALWAYS_INLINE inline AstNode* node_held(const M& member) {
  if constexpr (strip_storage<M>::storage == FieldStorage::vector) {
    return nullptr;
  } else if constexpr (strip_storage<M>::storage == FieldStorage::optional) {
    return member.has_value() ? node_held(*member) : nullptr;
  } else if constexpr (std::is_pointer_v<M>) {
    return member;
  } else if constexpr (tl_size<typename variant_node_alt<M>::type>::value ==
                       0) {
    return nullptr;
  } else {
    AstNode* node = nullptr;
    std::visit(
        [&](const auto& held) {
          if constexpr (ast_pointer_alt<std::remove_cvref_t<decltype(held)>>)
            node = held;
        },
        member);
    return node;
  }
}

// A write replaces the node a member held, which no longer has a parent that
// holds it.
CURSIV_ALWAYS_INLINE inline void let_go(AstNode* held, const AstNode* owner,
                                        bool& detached) {
  if (held != nullptr && held->parent == owner) {
    held->parent = nullptr;
    detached = true;
  }
}

}  // namespace cursiv::detail
