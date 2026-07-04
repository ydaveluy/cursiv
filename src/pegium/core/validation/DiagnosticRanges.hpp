#pragma once

#include <concepts>
#include <optional>
#include <string_view>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/AstFeatures.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/syntax-tree/CstUtils.hpp>

namespace pegium::validation {

/// Returns the source range of `node`, or `{0, 0}` when unavailable.
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_of(const AstNode &node) noexcept;

/// Returns the source range of a CST node view, or `{0, 0}` when invalid.
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_of(const CstNodeView &node) noexcept;

namespace detail {

template <auto Feature, typename Node>
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_for_feature_impl(const Node &node,
                       std::optional<std::size_t> index) noexcept {
  if (!node.hasCstNode()) {
    return {0u, 0u};
  }
  const auto cstNode = node.getCstNode();

  // Delegate to the single runtime implementation so the hidden-node skip and
  // assignment matching live in one place (the hand-rolled loop here had drifted
  // and skipped no hidden children).
  if (const auto featureNode = find_node_for_feature(
          cstNode, parser::detail::member_name_v<Feature>, index.value_or(0));
      featureNode.has_value()) {
    return range_of(*featureNode);
  }

  return {cstNode.getBegin(), cstNode.getEnd()};
}

} // namespace detail

/// Returns the range of the `index`-th occurrence assigned to `feature`.
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_for_feature(const AstNode &node, std::string_view feature,
                  std::size_t index) noexcept;

template <auto Feature, typename Node>
  requires pegium::detail::AstNodeFeature<Node, Feature>
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_for_feature(const Node &node) noexcept {
  return detail::range_for_feature_impl<Feature>(node, std::nullopt);
}

template <auto Feature, typename Node>
  requires pegium::detail::VectorAstNodeFeature<Node, Feature>
[[nodiscard]] std::pair<TextOffset, TextOffset>
range_for_feature(const Node &node, std::size_t index) noexcept {
  return detail::range_for_feature_impl<Feature>(node, index);
}

/// Returns the range of the `index`-th `keyword` literal parsed for `node`, or
/// the whole node's range when the keyword is absent from its CST (e.g. an
/// optional keyword that was not written).
[[nodiscard]] inline std::pair<TextOffset, TextOffset>
range_for_keyword(const AstNode &node, std::string_view keyword,
                  std::size_t index = 0) noexcept {
  if (!node.hasCstNode()) {
    return {0u, 0u};
  }
  const auto cstNode = node.getCstNode();
  if (const auto keywordNode = find_node_for_keyword(cstNode, keyword, index);
      keywordNode.has_value()) {
    return range_of(*keywordNode);
  }
  return {cstNode.getBegin(), cstNode.getEnd()};
}

} // namespace pegium::validation
