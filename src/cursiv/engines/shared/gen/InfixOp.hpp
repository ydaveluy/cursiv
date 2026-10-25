// An operator of a precedence table: its step and grouping reach the tokens it
// opens on once every production is laid.
#pragma once

#include <cstdint>
#include <type_traits>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv {

struct InfixOp {
  std::uint16_t prod = 0;   // `lhs -> left op right`, conditions around the op
  std::uint16_t owner = 0;  // the table, for the refusal's sentence
  std::uint8_t prec = 0;
  Association group = Association::left;
  std::uint8_t slot = 1;  // where the operator stands in `prod`
};

// The rule an operator names, `void` for a bare literal. Conditions around one
// element leave it the operator.
template <class Op>
struct infix_op_rule {
  using type = void;
};
template <class R>
struct infix_op_rule<Ref<R>> {
  using type = R;
};
template <class Read>
struct infix_op_read {
  using type = void;
};
template <class E>
struct infix_op_read<TypeList<E>> : infix_op_rule<E> {};
template <class... Es>
struct infix_op_rule<Seq<Es...>> : infix_op_read<detail::readers_t<Es...>> {};

// What an operator's site reads from. A rule that states no conversion, joins
// no parts and lands in a member no alternative is chosen for reads its span
// exactly as a bare literal does -- naming it must not mint a second row, or
// the ladder's productions stop sharing a class and the table grows one state
// per operator instead of one per step. `Resolve` is the rule a stage reads
// for the one named.
template <class Op, auto P,
          template <class> class Resolve = std::type_identity_t>
consteval auto infix_op_src_of() {
  using R = typename infix_op_rule<Op>::type;
  if constexpr (std::is_void_v<R>) {
    return std::type_identity<void>{};
  } else {
    using Read = Resolve<R>;
    if constexpr (std::is_void_v<detail::converter_rule_t<Read>> &&
                  !is_data_rule<Read> &&
                  !detail::is_variant_v<typename member_traits<
                      std::remove_cv_t<decltype(P)>>::type>)
      return std::type_identity<void>{};
    else
      return std::type_identity<R>{};
  }
}
template <class Op, auto P,
          template <class> class Resolve = std::type_identity_t>
using infix_op_src = typename decltype(infix_op_src_of<Op, P, Resolve>())::type;

}  // namespace cursiv
