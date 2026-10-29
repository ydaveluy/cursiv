#include "cursiv/core/workspace/DefaultAstNodeLocator.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

namespace {

// Which member of `container` holds `child`, and where. Not read off the
// child itself: here the grammar answers it, so the walk is over the
// container's members and stops at the first that yields this very node.
struct Slot {
  std::string_view property;
  std::uint32_t index = AstReflection::kNoIndex;
};

std::optional<Slot> slot_of(const AstReflection& reflection,
                            const AstNode& container, const AstNode& child) {
  std::optional<Slot> found;
  reflection.visitMembers(container,
                          [&](std::string_view property, std::uint32_t index,
                              const AstNode& candidate) {
                            if (!found.has_value() && &candidate == &child)
                              found = Slot{property, index};
                          });
  return found;
}

}  // namespace

std::string DefaultAstNodeLocator::getPathSegment(const AstNode& node) const {
  const AstNode* const container = node.parent;
  if (container == nullptr) return {};

  // Throws rather than yielding an empty segment: an empty one would make the
  // whole path empty, and an empty path reads back as the root. A node its
  // container does not hold is a broken tree, not the root.
  const std::optional<Slot> slot =
      slot_of(*services.astReflection, *container, node);
  if (!slot.has_value())
    throw std::invalid_argument(
        "cursiv: the container of this node does not hold it, so it has no "
        "path");

  std::string segment(slot->property);
  if (slot->index != AstReflection::kNoIndex) {
    segment += kIndexSeparator;
    segment += std::to_string(slot->index);
  }
  return segment;
}

std::string DefaultAstNodeLocator::getAstNodePath(const AstNode& node) const {
  // A detached AST has a parentless root too. Only the Document wrapper is
  // excluded from the path; being parentless does not make a node a Document.
  std::vector<const AstNode*> ancestors;
  for (const AstNode* current = &node;
       current->parent != nullptr &&
       current->parent->type != &ast_type_tag<Document>;
       current = current->parent)
    ancestors.push_back(current);
  std::string path;
  for (auto at = ancestors.rbegin(); at != ancestors.rend(); ++at) {
    path += kSegmentSeparator;
    path += getPathSegment(**at);
  }
  return path;
}

AstNode* DefaultAstNodeLocator::getAstNode(AstNode& node,
                                           std::string_view path) const {
  AstNode* at = &node;
  while (!path.empty() && at != nullptr) {
    if (path.front() == kSegmentSeparator) path.remove_prefix(1);
    const std::size_t end = path.find(kSegmentSeparator);
    const std::string_view segment = path.substr(0, end);
    path =
        end == std::string_view::npos ? std::string_view{} : path.substr(end);
    if (segment.empty()) continue;

    // `property` or `property@index`, which is the whole of the vocabulary.
    const std::size_t mark = segment.find(kIndexSeparator);
    const std::string_view property = segment.substr(0, mark);
    std::uint32_t wanted = AstReflection::kNoIndex;
    if (mark != std::string_view::npos) {
      const std::string_view digits = segment.substr(mark + 1);
      std::uint32_t parsed = 0;
      const auto [stop, err] =
          std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
      if (err != std::errc{} || stop != digits.data() + digits.size())
        return nullptr;
      wanted = parsed;
    }

    AstNode* next = nullptr;
    services.astReflection->visitMembers(
        *at, [&](std::string_view name, std::uint32_t index, AstNode& child) {
          if (next == nullptr && name == property && index == wanted)
            next = &child;
        });
    at = next;
  }
  return at;
}

}  // namespace cursiv
