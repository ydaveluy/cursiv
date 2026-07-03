#include <pegium/core/syntax-tree/CstUtils.hpp>

#include <algorithm>
#include <limits>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium {

namespace {

[[nodiscard]] CstNodeView next_after_subtree(const CstNodeView &node) noexcept {
  for (auto candidate = node.next(); candidate.valid(); candidate = candidate.next()) {
    if (candidate.getBegin() >= node.getEnd()) {
      return candidate;
    }
  }
  return {};
}

std::optional<CstNodeView> first_visible_descendant(const CstNodeView &node) {
  if (!node.valid() || node.isLeaf()) {
    return std::nullopt;
  }

  for (auto candidate = node.next();
       candidate.valid() && candidate.getBegin() < node.getEnd();) {
    if (candidate.isHidden()) {
      candidate = next_after_subtree(candidate);
      continue;
    }
    if (candidate.isLeaf()) {
      return candidate;
    }
    candidate = candidate.next();
  }
  return std::nullopt;
}

bool text_matches_keyword(std::string_view text,
                          std::string_view keyword) noexcept {
  if (text.size() != keyword.size()) {
    return false;
  }
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (utils::tolower(text[index]) != utils::tolower(keyword[index])) {
      return false;
    }
  }
  return true;
}

bool literal_matches_keyword(const grammar::Literal &literal,
                             std::string_view keyword) noexcept {
  const auto expected = literal.getValue();
  if (literal.isCaseSensitive()) {
    return expected == keyword;
  }
  return text_matches_keyword(expected, keyword);
}

// True when `child` is a visible Assignment node binding `feature`. Shared by
// the collect-all (`collect_feature_nodes`) and indexed-lookup
// (`find_node_for_feature`) walks so the filter lives in one place.
[[nodiscard]] bool child_assigns_feature(const CstNodeView &child,
                                         std::string_view feature) noexcept {
  if (child.isHidden() || child.getGrammarElement()->getKind() !=
                              grammar::ElementKind::Assignment) {
    return false;
  }
  return static_cast<const grammar::Assignment *>(child.getGrammarElement())
             ->getFeature() == feature;
}

void collect_feature_nodes(const CstNodeView &node, std::string_view feature,
                           std::vector<CstNodeView> &matches) {
  if (!node.valid()) {
    return;
  }
  for (const auto &child : node) {
    if (child_assigns_feature(child, feature)) {
      matches.push_back(child);
    }
  }
}

void collect_keyword_nodes(const CstNodeView &node, std::string_view keyword,
                           std::vector<CstNodeView> &matches,
                           std::size_t stopAfter =
                               std::numeric_limits<std::size_t>::max()) {
  if (!node.valid()) {
    return;
  }
  for (const auto &child : node) {
    if (matches.size() == stopAfter) {
      break;
    }
    if (child.isHidden()) {
      continue;
    }
    const auto candidate =
        child.isLeaf() ? std::optional<CstNodeView>(child)
                       : first_visible_descendant(child);
    if (!candidate.has_value() || candidate->isHidden() ||
        (!candidate->getGrammarElement() ||
         (candidate->getGrammarElement()->getKind() !=
              grammar::ElementKind::Literal &&
          candidate->getGrammarElement()->getKind() !=
              grammar::ElementKind::InfixOperator))) {
      continue;
    }

    const auto *grammarElement = candidate->getGrammarElement();
    if (grammarElement->getKind() == grammar::ElementKind::Literal) {
      if (const auto *literal =
              static_cast<const grammar::Literal *>(grammarElement);
          literal_matches_keyword(*literal, keyword)) {
        matches.push_back(*candidate);
      }
      continue;
    }

    const auto *operatorElement =
        static_cast<const grammar::InfixOperator *>(grammarElement);
    if (const auto *operatorRule = operatorElement->getOperator();
        operatorRule != nullptr &&
        operatorRule->getKind() == grammar::ElementKind::Literal &&
        literal_matches_keyword(
            *static_cast<const grammar::Literal *>(operatorRule), keyword)) {
      matches.push_back(*candidate);
      continue;
    }

    if (text_matches_keyword(candidate->getText(), keyword)) {
      matches.push_back(*candidate);
    }
  }
}

bool contains(const CstNodeView &parent, const CstNodeView &child) noexcept {
  return parent.valid() && child.valid() &&
         parent.getBegin() <= child.getBegin() &&
         child.getEnd() <= parent.getEnd();
}

std::optional<CstNodeView>
find_first_root_child_containing(const RootCstNode &root,
                                 const CstNodeView &target) noexcept {
  for (const auto child : root) {
    if (target.getBegin() < child.getBegin()) {
      return std::nullopt;
    }
    if (contains(child, target)) {
      return child;
    }
  }
  return std::nullopt;
}

std::optional<CstNodeView>
find_first_direct_child_containing(const CstNodeView &container,
                                   const CstNodeView &target) noexcept {
  if (!container.valid() || container.isLeaf() || !contains(container, target)) {
    return std::nullopt;
  }

  for (const auto child : container) {
    if (target.getBegin() < child.getBegin()) {
      return std::nullopt;
    }
    if (contains(child, target)) {
      return child;
    }
  }
  return std::nullopt;
}

std::optional<CstNodeView> find_direct_child_at_offset(const CstNodeView &node,
                                                       TextOffset offset) {
  if (!node.valid() || node.isLeaf()) {
    return std::nullopt;
  }

  for (const auto child : node) {
    const auto &childNode = child.node();
    if (offset < childNode.begin) {
      break;
    }
    if (!childNode.isHidden && offset <= childNode.end) {
      return child;
    }
  }

  return std::nullopt;
}

} // namespace

std::optional<std::string_view>
get_terminal_rule_name(const CstNodeView &node) noexcept {
  if (!node.valid() || !node.isLeaf()) {
    return std::nullopt;
  }

  const auto *grammarElement = node.getGrammarElement();
  if (grammarElement == nullptr ||
      grammarElement->getKind() != grammar::ElementKind::TerminalRule) {
    return std::nullopt;
  }
  return static_cast<const grammar::TerminalRule *>(grammarElement)->getName();
}

std::optional<CstNodeView> find_node_for_feature(const CstNodeView &node,
                                                 std::string_view feature) {
  return find_node_for_feature(node, feature, 0);
}

std::optional<CstNodeView> find_node_for_feature(const CstNodeView &node,
                                                 std::string_view feature,
                                                 std::size_t index) {
  if (!node.valid()) {
    return std::nullopt;
  }
  std::size_t seen = 0;
  for (const auto &child : node) {
    if (child_assigns_feature(child, feature) && seen++ == index) {
      return child;
    }
  }
  return std::nullopt;
}

std::vector<CstNodeView> find_nodes_for_feature(const CstNodeView &node,
                                                std::string_view feature) {
  std::vector<CstNodeView> matches;
  collect_feature_nodes(node, feature, matches);
  return matches;
}

std::optional<CstNodeView> find_node_for_keyword(const CstNodeView &node,
                                                 std::string_view keyword,
                                                 std::size_t index) {
  // Stop collecting once `index` is reached; the tail is never read here. Guard
  // the rare maximal index so `index + 1` cannot wrap to 0 (which would collect
  // nothing).
  const std::size_t stopAfter = index == std::numeric_limits<std::size_t>::max()
                                    ? std::numeric_limits<std::size_t>::max()
                                    : index + 1U;
  std::vector<CstNodeView> matches;
  collect_keyword_nodes(node, keyword, matches, stopAfter);
  if (index >= matches.size()) {
    return std::nullopt;
  }
  return matches[index];
}

std::vector<CstNodeView> find_nodes_for_keyword(const CstNodeView &node,
                                                std::string_view keyword) {
  std::vector<CstNodeView> matches;
  collect_keyword_nodes(node, keyword, matches);
  return matches;
}

std::optional<CstNodeView> find_node_at_offset(const CstNodeView &node,
                                               TextOffset offset) {
  if (!node.valid() || node.isHidden() || offset < node.getBegin() ||
      offset > node.getEnd()) {
    return std::nullopt;
  }

  auto current = node;
  while (true) {
    const auto child = find_direct_child_at_offset(current, offset);
    if (!child.has_value()) {
      break;
    }
    current = *child;
  }

  return current;
}

std::optional<CstNodeView> find_node_at_offset(const RootCstNode &root,
                                               TextOffset offset) {
  for (const auto current : root) {
    const auto &node = current.node();
    if (offset < node.begin) {
      return std::nullopt;
    }
    if (!node.isHidden && offset <= node.end) {
      return find_node_at_offset(current, offset);
    }
  }

  return std::nullopt;
}

std::optional<CstNodeView>
find_declaration_node_at_offset(const RootCstNode &root, TextOffset offset) {
  const auto text = root.getText();
  const auto size = static_cast<TextOffset>(text.size());
  auto adjustedOffset = std::min(offset, size);

  // When the cursor sits past the end or on a non-identifier codepoint, step
  // back onto the previous codepoint so a declaration touched on its right edge
  // still resolves. Classify whole UTF-8 codepoints, not raw bytes, so the lead
  // byte of a non-ASCII identifier (e.g. "é") is not mistaken for a separator.
  if (adjustedOffset > 0) {
    const char *const data = text.data();
    if (adjustedOffset >= size ||
        !utils::is_identifier_like_codepoint_at(data + adjustedOffset,
                                                data + size)) {
      adjustedOffset = static_cast<TextOffset>(
          utils::previous_codepoint_start(text, adjustedOffset));
    }
  }

  return find_node_at_offset(root, adjustedOffset);
}

std::optional<CstNodeView> find_first_leaf(const RootCstNode &root) noexcept {
  for (auto node = root.get(0); node.valid(); node = node.next()) {
    if (node.isLeaf()) {
      return node;
    }
  }
  return std::nullopt;
}

std::optional<CstNodeView> find_next_leaf(const CstNodeView &node) noexcept {
  if (!node.valid()) {
    return std::nullopt;
  }

  for (auto candidate = node.next(); candidate.valid(); candidate = candidate.next()) {
    if (candidate.getBegin() < node.getEnd()) {
      continue;
    }
    if (candidate.isLeaf()) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::vector<CstNodeView> get_interior_nodes(const CstNodeView &start,
                                            const CstNodeView &end) {
  if (!start.valid() || !end.valid() || &start.root() != &end.root()) {
    return {};
  }

  auto orderedStart = start;
  auto orderedEnd = end;
  if (orderedEnd.getBegin() < orderedStart.getBegin()) {
    std::swap(orderedStart, orderedEnd);
  }

  auto left = find_first_root_child_containing(start.root(), orderedStart);
  auto right = find_first_root_child_containing(start.root(), orderedEnd);
  while (left.has_value() && right.has_value() && left->id() == right->id()) {
    if (left->isLeaf()) {
      return {};
    }
    left = find_first_direct_child_containing(*left, orderedStart);
    right = find_first_direct_child_containing(*right, orderedEnd);
  }

  if (!left.has_value() || !right.has_value()) {
    return {};
  }

  std::vector<CstNodeView> interior;
  for (auto node = left->nextSibling();
       node.valid() && node.id() != right->id();
       node = node.nextSibling()) {
    interior.push_back(node);
  }
  return interior;
}

} // namespace pegium
