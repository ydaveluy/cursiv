// What an operator of a precedence table reads its value from.
#pragma once

#include <cstdint>
#include <type_traits>

#include "cursiv/grammar/Association.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/engine/grammar/Row.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// An operator with a single text source inherits that source's conversion.
template <class Payloads>
struct infix_op_rule { using type = void; };
template <class R>
struct infix_op_rule<TypeList<detail::text_payload<R>>> { using type = R; };

// What an operator's site reads from. A rule with no conversion, no parts and
// no chosen alternative reads its span as a bare literal does, on the same
// row. `Resolve` is the rule a stage reads for the one named.
template <class Op, auto P,
          template <class> class Resolve = std::type_identity_t>
consteval auto infix_op_src_of() {
  using R = typename infix_op_rule<typename detail::payload_of<Op>::type>::type;
  if constexpr (std::is_void_v<R>) {
    return std::type_identity<void>{};
  } else {
    using Read = Resolve<R>;
    if constexpr (std::is_void_v<detail::converter_rule_t<Read>> &&
                  !detail::is_data_rule<Read> &&
                  !detail::is_variant_v<typename detail::member_traits<
                      std::remove_cv_t<decltype(P)>>::type>)
      return std::type_identity<void>{};
    else
      return std::type_identity<R>{};
  }
}
template <class Op, auto P,
          template <class> class Resolve = std::type_identity_t>
using infix_op_src = typename decltype(infix_op_src_of<Op, P, Resolve>())::type;

// The payload an operator's write converts, read through the stage.
template <class St, class Op, auto P>
using infix_src =
    typename src_rule<St, infix_op_src<Op, P, St::template rule>>::type;

}  // namespace cursiv::engine
