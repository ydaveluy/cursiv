// The AST registries of one grammar, from the actions its lowering publishes:
// each action is a constant descriptor of the class, member and conversion it
// names, and `build_ast_registry` settles the indices. What the grammar must
// satisfy to be built at all is refused here, where the types are.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/AstRegistryBuilder.hpp"
#include "cursiv/engines/shared/gen/GrammarRefusals.hpp"
#include "cursiv/engines/shared/gen/GrammarBindings.hpp"
#include "cursiv/engines/shared/gen/Row.hpp"
#include "cursiv/engines/shared/gen/RowOf.hpp"
#include "cursiv/engines/shared/gen/WritesOnce.hpp"
#include "cursiv/engines/shared/gen/ast_refusals.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"
#include "cursiv/engines/shared/runtime/AstConvThunks.hpp"
#include "cursiv/engines/shared/runtime/ast_conv.hpp"
#include "cursiv/engines/shared/runtime/ast_fields.hpp"

namespace cursiv {

namespace detail {

// A data member pointer holds the member's offset in its class, which orders
// the members of one class as it declares them.
template <auto P>
std::ptrdiff_t field_offset() {
  constexpr auto member = P;
  static_assert(sizeof member == sizeof(std::ptrdiff_t),
                "this ABI's data member pointer is not an offset");
  std::ptrdiff_t offset = 0;
  std::memcpy(&offset, &member, sizeof offset);
  return offset;
}

template <class T>
consteval const AstTypeTag* tag_of() {
  if constexpr (std::is_base_of_v<AstNode, T>)
    return &ast_type_tag<T>;
  else
    return nullptr;
}

template <class T>
inline constexpr AstTypeDesc kType{tag_of<T>()};

template <class Src, class M>
inline constexpr AstConvDesc kConv{};

template <class L>
struct TargetsOf;
template <class... Ts>
struct TargetsOf<TypeList<Ts...>> {
  static constexpr const AstTypeDesc* kStore[1 + sizeof...(Ts)] = {nullptr, &kType<Ts>...};
  static constexpr const AstTypeDesc* const* kAll = kStore + 1;
};

template <auto P>
consteval AstFieldDesc make_field() {
  using F = field_tag<P>;
  using Held = typename held_targets<typename F::held>::type;
  AstFieldDesc d;
  d.name = F::name;
  d.offset = &field_offset<P>;
  d.nature = F::nature;
  d.storage = F::storage;
  d.owner = &kType<typename F::owner>;
  d.targets = TargetsOf<Held>::kAll;
  d.targetCount = static_cast<std::uint32_t>(tl_size<Held>::value);
  return d;
}
template <auto P>
inline constexpr AstFieldDesc kField = make_field<P>();

// The rules a fold stands for: its layers convert text although no call names
// them.
template <class RR>
struct folded_layers {
  using type = TypeList<>;
};
template <class RR>
  requires requires { typename RR::fold_members; }
struct folded_layers<RR> {
  using type = typename RR::fold_members;
};

// Whether a data source's reachable terminals and data rules, under this
// grammar's overrides, convert text: only those supply it.
template <class Stage, class... Rs>
consteval bool converted_parts(TypeList<Rs...>) {
  return ([] {
    using R = typename Stage::template rule<declared_of_t<Rs>>;
    if constexpr (is_terminal_rule<R> || is_data_rule<R>)
      return DenotesText<converter_rule_t<R>>;
    else
      return false;
  }() || ...);
}

template <class Stage, class R>
consteval AstRowDesc make_row() {
  using Row = row_of<R>;
  AstRowDesc d;
  AstRowGen& g = d.gen;
  g.kind = Row::kind;
  if constexpr (Row::kind == WriteKind::convert) {
    // The data rule an `as<X>(e)` stands for has no name of its own: its
    // record keeps the name of what it reads.
    if constexpr (!is_anonymous_rule<typename Row::type> ||
                  slot_empty<typename Row::type>)
      g.rule = type_name<typename Row::type>();
    g.dataRule = is_data_rule<typename Row::type>;
    // The text path runs an override's converter, which the row type drops.
    using O = ov::slot_decl_t<typename Row::source>;
    using Cv = site_conv_t<typename Row::type, O>;
    if constexpr ((is_terminal_rule<typename Row::type> ||
                   is_data_rule<typename Row::type>) &&
                  DenotesText<Cv>) {
      d.textType = &kType<declared_of_t<typename Row::type>>;
      d.textOverride = !std::is_void_v<O>;
      g.textInput = ConvertsParts<Cv> || ConvertsPartsMem<Cv> ? TextInput::parts
                    : converts_input<Cv>                     ? TextInput::text
                                                             : TextInput::none;
    }
    using Src = typename Row::source;
    if constexpr (is_terminal_rule<Src> || is_data_rule<Src>) {
      g.passes = is_pass_conv<converter_rule_t<Src>>;
      g.valued = !g.passes &&
                 !std::is_same_v<rule_type_t<Src>, std::string_view>;
      if (g.passes || g.valued)
        d.valueType = &kType<declared_of_t<typename Row::type>>;
    }
  } else {
    d.decl = &kType<typename Row::type>;
  }
  if constexpr (Row::kind == WriteKind::create ||
                Row::kind == WriteKind::climb || Row::kind == WriteKind::nest) {
    static_assert(::BuildableNode<typename Row::type>(),
                  "a rule of this grammar builds a class it cannot: the "
                  "message above says which");
    d.built = true;
  }
  if constexpr (Row::kind == WriteKind::close) d.named = true;
  using F = typename row_field<R>::type;
  if constexpr (!std::is_void_v<F>) {
    d.field = &kField<F::ptr>;
    g.feature = F::name;
    if constexpr (!std::is_void_v<typename Row::narrow>)
      d.narrow = &kType<typename Row::narrow>;
  }
  // An assign or append converts a payload source into its member.
  if constexpr (row_writes<R> && !std::is_void_v<typename Row::source>) {
    using Src = typename Row::source;
    using M = conv_target_t<F>;
    // A converter is named by the rule the author declared.
    using D = declared_of_t<typename Src::rule>;
    using O = ov::slot_decl_t<typename Src::rule>;
    static_assert(
        std::is_same_v<converter_rule_t<typename Src::rule>, site_conv_t<D, O>>,
        "the converter this site runs is not the one the author's "
        "declarations name: a stage minted it from something else");
    d.conv = &kConv<Src, M>;
    if constexpr (std::is_void_v<D>)
      g.convName = "void";
    else
      d.convType = &kType<D>;
    d.convOverride = !std::is_void_v<O>;
    g.convRank = conv_alt_rank<M, Src>();
    g.convJoins = joins_parts<typename Src::rule>;
    using Site = site_conv_t<D, O>;
    g.convParts = ConvertsParts<Site> || ConvertsPartsMem<Site>;
    g.convPasses = is_pass_conv<Site>;
    if constexpr (joins_parts<typename Src::rule>) {
      using Inside = dedupe_t<
          tl_cat_t<typename reached_refs<Stage, D>::type,
                   typename folded_layers<typename Stage::template rule<D>>::type>>;
      using Reached = typename closure<Stage, Inside, Inside>::type;
      g.convTextParts = converted_parts<Stage>(Reached{});
    }
  }
  return d;
}

template <class Stage, class R>
inline constexpr AstRowDesc kRow = make_row<Stage, R>();

template <class Stage, class... Rs>
inline constexpr const AstRowDesc* kRows[1 + sizeof...(Rs)] = {nullptr, &kRow<Stage, Rs>...};

}  // namespace detail

template <class G, class Low>
AstRegistry ast_registry(std::string_view grammar = detail::raw_type_name<G>()) {
  using Rows = typename Low::actions;
  using Stage = typename Low::stage;
  // Grammar law: the entry rule produces a node, which is what lets the
  // builder's `result()` read the bottom of the stack instead of searching it.
  // A terminal or data-rule entry would push nothing.
  static_assert(is_node_rule<typename Normalize<G>::entry> ||
                    is_infix_rule<typename Normalize<G>::entry>,
                "the entry rule of this grammar builds no AST node: it is a "
                "terminal or a data rule -- make `entry` a rule that builds "
                "the root node");
  // An infix rule synthesizes its body, so a mismatched operand carries no
  // directive to refuse it: the check lives on the rule and is put in force
  // here, where the rules the build reached are named.
  static_assert(
      detail::infix_rules_ok<Stage, Low::rewrite::template infix_src>(
          typename Low::rules{}),
      "an infix rule of this grammar is refused: the message above says which");
  static_assert(
      detail::bases_stated(typename detail::ast_classes<Rows>::type{}));
  // Every directive of every rule, asked of the member it names here: the
  // builder would otherwise meet the mistake one position at a time, with no
  // member left to name in the message.
  static_assert(
      []<class... Rs>(TypeList<Rs...>) consteval {
        return (
            (detail::payloads_ok<typename Stage::template body<Rs>>::value &&
             detail::writes_once_ok<Stage, Rs,
                                    typename Stage::template body<Rs>>) &&
            ...);
      }(typename Low::rules{}),
      "a directive of this grammar names a member that cannot take its "
      "value: the message above says which");
  const auto rows = [&]<class... Rs>(TypeList<Rs...>) {
    return std::span<const AstRowDesc* const>(detail::kRows<Stage, Rs...> + 1,
                                              sizeof...(Rs));
  }(Rows{});
  const auto types = []<class... Ts>(TypeList<Ts...>) {
    return std::array<const AstTypeDesc*, sizeof...(Ts)>{&detail::kType<Ts>...};
  }(typename detail::GrammarBindings<G>::types{});
  const auto rules = []<class... Rs>(TypeList<Rs...>) {
    return std::array<const AstTypeDesc*, sizeof...(Rs)>{&detail::kType<Rs>...};
  }(typename detail::GrammarBindings<G>::rules{});
  return build_ast_registry(rows, types, rules, grammar);
}

}  // namespace cursiv
