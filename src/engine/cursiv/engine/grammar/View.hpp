// A grammar as the generator reads it: its image, with the row each
// directive writes and the stage each rule is read through, as constant
// descriptors.
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "cursiv/engine/grammar/Distribute.hpp"
#include "cursiv/engine/grammar/Facts.hpp"
#include "cursiv/engine/grammar/ImageOf.hpp"
#include "cursiv/engine/grammar/InfixOp.hpp"
#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/engine/grammar/Row.hpp"

namespace cursiv::engine::gen {

template <class G>
struct View {
  using grammar = G;
  using stage = typename Facts<G>::stage;
};

template <class R>
inline constexpr const void* kRowPtr = &kRow<R>;

template <class V, class Op, class RR>
inline constexpr ImageOpDesc kOp = {
    &kNode<V, Op>,
    kRowPtr<owned_row<RR::op_pointer,
                      infix_src<typename V::stage, Op, RR::op_pointer>>>};

template <class V, class RR, class Ops>
struct OpsOf;
template <class V, class RR, class... Ops>
struct OpsOf<V, RR, TypeList<Ops...>> {
  static constexpr ImageOpDesc kAll[] = {kOp<V, Ops, RR>...};
};

template <class V, class RR, class L>
consteval ImageStageLevelDesc stage_level() {
  using Alts = typename alts_list<typename L::op_expr>::type;
  ImageStageLevelDesc d;
  d.group = L::kGroup;
  d.ops = OpsOf<V, RR, Alts>::kAll;
  d.count = static_cast<std::uint32_t>(tl_size<Alts>::value);
  return d;
}

template <class V, class RR, class Lvls, class Is>
struct StageLevelsOf;
template <class V, class RR, class Lvls, std::size_t... Is>
struct StageLevelsOf<V, RR, Lvls, std::index_sequence<Is...>> {
  static constexpr ImageStageLevelDesc kAll[] = {
      stage_level<V, RR, typename Lvls::template at<Is>>()...};
};

// A rule as the stage reads it: its kind, its body with one write per
// alternative, and the rows it closes with.
template <class V, class D>
consteval ImageStageDesc stage_desc() {
  using St = typename V::stage;
  using RR = typename St::template rule<D>;
  ImageStageDesc s;
  if constexpr (is_infix_rule<RR>) {
    using Lvls = typename RR::levels;
    s.kind = ImageRuleKind::infix;
    s.returns = dump_type_name<typename RR::node_type>();
    s.members[0] = detail::member_identifier<RR::left_pointer>();
    s.members[1] = detail::member_identifier<RR::op_pointer>();
    s.members[2] = detail::member_identifier<RR::right_pointer>();
    s.primary = &kRule<V, declared_of_t<typename RR::primary>>;
    s.climbRow = kRowPtr<Row<WriteKind::climb, typename RR::node_type>>;
    s.leftRow = kRowPtr<owned_row<RR::left_pointer>>;
    s.rightRow = kRowPtr<owned_row<RR::right_pointer>>;
    s.levels =
        StageLevelsOf<V, RR, Lvls, std::make_index_sequence<Lvls::size>>::kAll;
    s.levelCount = static_cast<std::uint32_t>(Lvls::size);
  } else {
    if constexpr (kIsTerminalRule<RR>) {
      s.kind = ImageRuleKind::terminal;
      s.body = &kNode<
          V, typename ResolveOverride<typename V::grammar>::template body<D>>;
    } else {
      s.kind = is_data_rule<RR> ? ImageRuleKind::data : ImageRuleKind::node;
      s.body = &kNode<V, normalize_writes_t<typename St::template body<D>>>;
    }
    if constexpr (is_node_rule<RR> ||
                  !std::is_same_v<rule_type_t<RR>, std::string_view>)
      s.returns = dump_type_name<rule_type_t<RR>>();
    s.convertRow = kRowPtr<typename ConvertOf<RR>::row>;
    if constexpr (is_node_rule<RR>) s.closeRow = kRowPtr<close_row_t<RR>>;
  }
  return s;
}
template <class V, class D>
inline constexpr ImageStageDesc kStage = stage_desc<V, D>();

template <class... Rs>
inline constexpr const RowDesc* kRows[1 + sizeof...(Rs)] = {nullptr,
                                                            &kRow<Rs>...};

template <class G>
consteval LowerDesc lower_desc() {
  LowerDesc d;
  d.rows = []<class... Rs>(TypeList<Rs...>) {
    return std::span<const RowDesc* const>(kRows<Rs...> + 1, sizeof...(Rs));
  }(typename Facts<G>::rows{});
  d.entry =
      &kRule<View<G>, declared_of_t<typename Facts<G>::stage::entry>>;
  return d;
}
template <class G>
inline constexpr LowerDesc kLowerDesc = lower_desc<G>();

template <class G, class D>
inline constexpr const void* kRowOf<View<G>, D> =
    &kRow<typename directive_row<typename View<G>::stage, D>::type>;

template <class G, class D>
  requires(!is_grammar<D>)
inline constexpr const ImageStageDesc* kStageOf<View<G>, D> = &kStage<View<G>, D>;

}  // namespace cursiv::engine::gen
