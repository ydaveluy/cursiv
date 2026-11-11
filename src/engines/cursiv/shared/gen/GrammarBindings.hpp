// Source-level types shared by a generator and its emitted translation units.
// Indices preserve the actual C++ types, including the types of auto arguments.
#pragma once

#include <cstddef>
#include <type_traits>

#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/shared/gen/Row.hpp"
#include "cursiv/shared/gen/RuleRows.hpp"
#include "cursiv/shared/gen/reached.hpp"
#include "cursiv/shared/runtime/ast_fields.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

template <class R>
struct row_classes;
template <WriteKind K, class T, class M, class N, class Src>
struct row_classes<Row<K, T, M, N, Src>> {
  using type = cond_t<K == WriteKind::convert, TypeList<>, TypeList<T>>;
};
template <WriteKind K, class T, auto P, class N, class Src>
struct row_classes<Row<K, T, Wrote<P>, N, Src>> {
  using type =
      tl_cat_t<TypeList<T>, TypeList<typename field_tag<P>::owner>,
               typename held_targets<typename field_tag<P>::held>::type>;
};
template <class L>
struct ast_classes;
template <class... Rs>
struct ast_classes<TypeList<Rs...>>
    : dedupe<tl_cat_t<typename row_classes<Rs>::type...>> {};

// Only the operator's owner matters to this list, not its conversion source.
template <class, class, auto>
struct BindingInfixSource {};
template <class L>
struct binding_narrows;
template <class... Rs>
struct binding_narrows<TypeList<Rs...>>
    : tl_cat<cond_t<std::is_void_v<typename row_of<Rs>::narrow>, TypeList<>,
                    TypeList<typename row_of<Rs>::narrow>>...> {};

template <class G>
struct GrammarBindings {
  using stage = ResolveOverride<G>;
  using rules = reached_t<stage>;
  using rows = typename AllRows<stage, rules, BindingInfixSource>::type;
  using types = dedupe_t<tl_cat_t<typename ast_classes<rows>::type,
                                typename binding_narrows<rows>::type>>;
};

template <class G, std::size_t I>
using ast_bound_t = tl_at_t<I, typename GrammarBindings<G>::types>;

template <class G, std::size_t I>
using rule_bound_t = tl_at_t<I, typename GrammarBindings<G>::rules>;

template <class G, std::size_t I>
using rule_staged_t =
    typename GrammarBindings<G>::stage::template rule<rule_bound_t<G, I>>;

template <class G, std::size_t I>
using rule_override_t = detail::ov::slot_decl_t<rule_staged_t<G, I>>;

}  // namespace cursiv::engine
