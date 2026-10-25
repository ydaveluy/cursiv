// How a converted payload lands in a member: the type it converts to, the
// alternative a variant takes, the converter a site runs. The generator's
// registries and the builder's thunks read one answer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/runtime/ast_fields.hpp"

namespace cursiv::detail {

// Where a text payload lands: a view for a reference, the member's held type
// otherwise, a variant included. Naming the whole variant here is what lets the
// writer stay per-member while the alternative stays per-element.
template <class F>
using conv_target_t =
    cond_t<is_ref_nature(F::nature), AstNode::string_view, typename F::held>;

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
template <class M, class Src>
struct conv_alt<M, Src, true> {
  using type = tl_at_t<
      0, typename variant_cands<Src, typename variant_alts<M>::type>::type>;
};

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
  if constexpr (is_variant_v<M>)
    return index_of<typename conv_alt<M, Src>::type,
                    typename variant_alts<M>::type>::value;
  else
    return 0;
}

// Constructible, always: `admits` asks for exactly that, so a type assignable
// from V but not constructible from it never reaches here.
template <class M, class A, class V>
void conv_store(M& slot, V&& value) {
  if constexpr (is_variant_v<M>)
    slot.template emplace<A>(A(std::forward<V>(value)));
  else
    slot = M(std::forward<V>(value));
}

// The converter one site runs, from the declarations an author wrote: the
// override whose slot decides it (`slot_decl_t`) -- none where that slot names
// a bare target -- or, where no override fills one, the declared rule's. The
// rule a stage minted over them has no spelling.
template <class Declared, class Over>
struct site_conv {
  using type = cond_t<HasConverter<conv_t<Over>>, conv_t<Over>, void>;
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
template <class L, class X>
bool emplace_node_alt(std::uint16_t alt, X& slot, AstNode* value) {
  return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    bool done = false;
    ((Is == alt ? (void)(slot.template emplace<tl_at_t<Is, L>>(
                             static_cast<tl_at_t<Is, L>>(value)),
                         done = true)
                : (void)0),
     ...);
    return done;
  }(std::make_index_sequence<tl_size<L>::value>{});
}

// A write to a single-valued member replaces the node it holds, which no
// longer has a parent that holds it. Where the write goes through a
// temporary, the caller detaches what the member held once the write stands.
template <class X>
CURSIV_ALWAYS_INLINE inline void detach_held(const X& slot,
                                             const AstNode* owner,
                                             bool& detached) {
  if constexpr (std::is_pointer_v<X>) {
    if (slot != nullptr && slot->parent == owner) {
      slot->parent = nullptr;
      detached = true;
    }
  } else {
    std::visit(
        [&](auto& held) {
          if constexpr (ast_pointer_alt<std::remove_cvref_t<decltype(held)>>)
            if (held != nullptr && held->parent == owner) {
              held->parent = nullptr;
              detached = true;
            }
        },
        slot);
  }
}

}  // namespace cursiv::detail
