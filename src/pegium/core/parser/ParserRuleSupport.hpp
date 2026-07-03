#pragma once

/// Helper routines backing parser rule AST materialization.

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/Create.hpp>
#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/grammar/Nest.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <pegium/core/syntax-tree/AstArena.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <array>
#include <cassert>
#include <vector>

namespace pegium::parser::detail {

// T-independent body of ParserRule materialization. The only spots that need the
// concrete rule-result type are node creation (delegated to `createNode`) and
// the final downcast (done by the templated wrapper below), so one non-template
// body serves every rule type instead of one instantiation per type.
[[nodiscard]] inline AstNode *
build_parser_rule_node(const CstNodeView &node, const ValueBuildContext &context,
                       AstNode *(*createNode)(AstArena &)) {
  auto buildContext = context;
  std::vector<ReferenceHandle> localReferences;
  if (buildContext.references == nullptr) {
    buildContext.references = &localReferences;
  }
  assert(buildContext.arena != nullptr &&
         "ParserRule materialization requires a valid AstArena pointer.");
  AstArena &arena = *buildContext.arena;
  AstNode *currentNode = nullptr;
  struct PendingAssignmentEntry {
    const grammar::Assignment *assignment;
    NodeId nodeId;
  };
  std::array<PendingAssignmentEntry, 8> pendingInlineEntries{};
  std::size_t pendingInlineCount = 0;
  std::vector<PendingAssignmentEntry> pendingOverflowEntries;
  const auto &root = node.root();

  const auto addPendingAssignment =
      [&pendingInlineCount, &pendingInlineEntries,
       &pendingOverflowEntries](const grammar::Assignment *assignment,
                                NodeId nodeId) {
    if (pendingInlineCount < pendingInlineEntries.size()) {
      pendingInlineEntries[pendingInlineCount++] = PendingAssignmentEntry{
          .assignment = assignment,
          .nodeId = nodeId,
      };
      return;
    }
    pendingOverflowEntries.push_back(PendingAssignmentEntry{
        .assignment = assignment,
        .nodeId = nodeId,
    });
      };

  const auto applyPendingAssignments =
      [&pendingInlineCount, &pendingInlineEntries, &pendingOverflowEntries,
       &root, &buildContext](AstNode *targetNode) {
    if (pendingInlineCount == 0 && pendingOverflowEntries.empty()) [[likely]] {
      return;
    }
    assert(targetNode != nullptr);
    if (pendingInlineCount == 1 && pendingOverflowEntries.empty()) [[likely]] {
      const auto &entry = pendingInlineEntries[0];
      entry.assignment->execute(targetNode, CstNodeView(&root, entry.nodeId),
                                buildContext);
      return;
    }
    for (std::size_t index = 0; index < pendingInlineCount; ++index) {
      const auto &entry = pendingInlineEntries[index];
      entry.assignment->execute(targetNode, CstNodeView(&root, entry.nodeId),
                                buildContext);
    }
    for (const auto &entry : pendingOverflowEntries) {
      entry.assignment->execute(targetNode, CstNodeView(&root, entry.nodeId),
                                buildContext);
    }
      };

  const auto applyAndClearPendingAssignments =
      [&applyPendingAssignments, &pendingInlineCount,
       &pendingOverflowEntries](AstNode *targetNode) {
    applyPendingAssignments(targetNode);
    pendingInlineCount = 0;
    pendingOverflowEntries.clear();
      };

  if (node.isLeaf()) {
    auto *leafNode = createNode(arena);
    leafNode->setCstNode(node);
    // A leaf node carries no pending assignments by construction: the only
    // addPendingAssignment calls happen inside the child loop below, which
    // this branch returns before reaching.
    return leafNode;
  }

  for (const auto view : node) {
    const auto &cstNode = view.node();
    if (cstNode.isHidden) {
      continue;
    }
    using enum grammar::ElementKind;
    switch (view.getGrammarElement()->getKind()) {
    case Assignment: {
      addPendingAssignment(
          static_cast<const grammar::Assignment *>(cstNode.grammarElement),
          view.id());
      break;
    }
    case Create: {
      currentNode =
          static_cast<const grammar::Create *>(cstNode.grammarElement)
              ->getValue(arena);
      assert(currentNode != nullptr);
      currentNode->setCstNode(node);
      break;
    }
    case Nest: {
      if (currentNode == nullptr) {
        currentNode = createNode(arena);
        currentNode->setCstNode(node);
      }
      applyAndClearPendingAssignments(currentNode);
      currentNode =
          static_cast<const grammar::Nest *>(cstNode.grammarElement)
              ->getValue(currentNode, arena);
      assert(currentNode != nullptr);
      currentNode->setCstNode(node);
      break;
    }
    case ParserRule: {
      currentNode =
          static_cast<const grammar::ParserRule *>(cstNode.grammarElement)
              ->getValue(view, buildContext);
      assert(currentNode != nullptr && currentNode->hasCstNode());
      break;
    }
    case InfixRule: {
      const auto *infixRule =
          static_cast<const grammar::InfixRule *>(cstNode.grammarElement);
      applyAndClearPendingAssignments(currentNode);
      currentNode = infixRule->getValue(view, currentNode, buildContext);
      assert(currentNode != nullptr && currentNode->hasCstNode());
      break;
    }
    default:
      break;
    }
  }

  if (currentNode == nullptr) {
    currentNode = createNode(arena);
    currentNode->setCstNode(node);
  }
  applyPendingAssignments(currentNode);
  assert(currentNode != nullptr);
  return currentNode;
}

template <typename T> struct ParserRuleBuildSupport {
  [[nodiscard]] static T *get_raw_value(const CstNodeView &node,
                                        const ValueBuildContext &context) {
    return static_cast<T *>(build_parser_rule_node(
        node, context,
        +[](AstArena &arena) -> AstNode * { return arena.template create<T>(); }));
  }
};

} // namespace pegium::parser::detail
