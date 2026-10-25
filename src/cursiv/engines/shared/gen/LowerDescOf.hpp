// What a lowering reads of a grammar, as constant descriptors: the stage each
// rule is read through, the actions it publishes, the symbols in the order they
// are numbered. A view names the grammar and the rewrite its engine reads the
// rules through.
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/ImageOf.hpp"
#include "cursiv/engines/shared/gen/AstRowOf.hpp"
#include "cursiv/engines/shared/gen/Distribute.hpp"
#include "cursiv/engines/shared/gen/LowerDesc.hpp"
#include "cursiv/engines/shared/gen/LowerFacts.hpp"
#include "cursiv/engines/shared/gen/RuleRows.hpp"
#include "cursiv/engines/shared/gen/optional_shape.hpp"
#include "cursiv/engines/shared/gen/spec_row.hpp"

namespace cursiv::detail {

struct LoweringTag {};

template <class G, class Rewrite, bool EntryCreate>
struct LoweringView : ViewBase, LoweringTag {
  using grammar = G;
  using rewrite = Rewrite;
  using facts = LowerFacts<G, Rewrite>;
  using stage = typename facts::stage;
  static constexpr bool entry_create = EntryCreate;
};

template <class V>
concept lowering_view = std::is_base_of_v<LoweringTag, V>;

template <class V, class D>
  requires lowering_view<V>
inline constexpr const void* kRowOf<V, D> =
    &kRow<typename V::stage, typename directive_row<typename V::stage, D>::type>;

template <class St, class R>
inline constexpr const void* kRowPtr = &kRow<St, R>;

template <class V, class St, class Op, class RR>
inline constexpr ImageOpDesc kOp = {
    &kNode<V, Op>,
    kRowPtr<St, owned_row<RR::op_pointer,
                          typename V::rewrite::template infix_src<St, Op, RR::op_pointer>>>};

template <class V, class St, class RR, class Ops>
struct OpsOf;
template <class V, class St, class RR, class... Ops>
struct OpsOf<V, St, RR, TypeList<Ops...>> {
  static constexpr ImageOpDesc kAll[] = {kOp<V, St, Ops, RR>...};
};

template <class V, class St, class RR, class Lvls, class Is>
struct StageLevelsOf;
template <class V, class St, class RR, class Lvls, std::size_t... Is>
struct StageLevelsOf<V, St, RR, Lvls, std::index_sequence<Is...>> {
  static constexpr ImageStageLevelDesc kAll[] = {
      {Lvls::template at<Is>::kGroup == Association::right ? ImageGroup::right
       : Lvls::template at<Is>::kGroup == Association::non ? ImageGroup::non
                                                           : ImageGroup::left,
       OpsOf<V, St, RR,
             typename alts_list<
                 typename Lvls::template at<Is>::op_expr>::type>::kAll,
       static_cast<std::uint32_t>(tl_size<typename alts_list<
                                      typename Lvls::template at<Is>::op_expr>::
                                      type>::value)}...};
};

template <class V, class D>
consteval ImageStageDesc lower_stage_desc() {
  using G = typename V::grammar;
  using St = typename V::stage;
  using RR = typename St::template rule<D>;
  ImageStageDesc s;
  if constexpr (is_infix_rule<RR>) {
    using Lvls = typename RR::levels;
    s.kind = ImageRuleKind::infix;
    s.primary = &kRule<V, declared_of_t<typename RR::primary>>;
    s.climbRow = kRowPtr<St, Row<WriteKind::climb, typename RR::node_type>>;
    s.leftRow = kRowPtr<St, owned_row<RR::left_pointer>>;
    s.rightRow = kRowPtr<St, owned_row<RR::right_pointer>>;
    s.levels = StageLevelsOf<V, St, RR, Lvls,
                             std::make_index_sequence<Lvls::size>>::kAll;
    s.levelCount = static_cast<std::uint32_t>(Lvls::size);
  } else {
    if constexpr (is_terminal_rule<RR>) {
      s.kind = ImageRuleKind::terminal;
      s.body = &kNode<V, typename ResolveOverride<G>::template body<D>>;
    } else {
      s.kind = is_data_rule<RR> ? ImageRuleKind::data : ImageRuleKind::node;
      using Body = typename St::template body<D>;
      s.body = &kNode<V, normalize_writes_t<Body>>;
    }
    s.folded = requires { RR::ll_folded; };
    s.convertRow = kRowPtr<St, typename ConvertOf<RR>::row>;
    if constexpr (is_node_rule<RR>)
      s.closeRow = kRowPtr<St, close_row_t<RR>>;
  }
  return s;
}

template <class V, class D>
inline constexpr ImageStageDesc kLowerStage = lower_stage_desc<V, D>();

template <class V, class D>
consteval const ImageStageDesc* lower_stage() {
  if constexpr (is_grammar<D>)
    return nullptr;
  else
    return &kLowerStage<V, D>;
}
template <class V, class D>
  requires lowering_view<V>
inline constexpr const ImageStageDesc* kStageOf<V, D> = lower_stage<V, D>();

template <class V, class L>
constexpr std::span<const ImageRuleDesc* const> rule_list() {
  return {RulesOf<V, L>::kAll, tl_size<L>::value};
}

template <class V>
consteval LowerDesc lower_desc() {
  using G = typename V::grammar;
  using F = typename V::facts;
  using St = typename F::stage;
  LowerDesc d;
  d.terms = rule_list<V, typename F::terms>();
  d.predicateTerms = rule_list<V, typename F::predicate_terms>();
  d.nts = rule_list<V, typename F::nts>();
  d.rows = []<class... Rs>(TypeList<Rs...>) {
    return std::span<const AstRowDesc* const>(kRows<St, Rs...> + 1,
                                              sizeof...(Rs));
  }(typename F::rows{});
  d.entry = &kRule<V, declared_of_t<typename St::entry>>;
  d.bound = class_of_bound<
      std::remove_cvref_t<decltype(St::word_boundary)>>::value;
  d.indentOpen =
      &kNode<V, std::remove_cvref_t<decltype(G::indent_open)>>;
  d.indentClose =
      &kNode<V, std::remove_cvref_t<decltype(G::indent_close)>>;
  if constexpr (!std::is_void_v<typename G::indent_newline>)
    d.indentNewline =
        &kRule<V, declared_of_t<typename G::indent_newline>>;
  if constexpr (V::entry_create &&
                is_node_rule<typename St::template rule<
                    declared_of_t<typename St::entry>>>)
    d.entryCreate =
        kRowPtr<St, Row<WriteKind::create, rule_type_t<typename St::entry>>>;
  d.optionalGate = kOptionalGate;
  return d;
}


}  // namespace cursiv::detail
