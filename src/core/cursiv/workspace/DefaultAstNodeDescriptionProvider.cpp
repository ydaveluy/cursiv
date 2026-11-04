#include "cursiv/workspace/DefaultAstNodeDescriptionProvider.hpp"

#include <optional>
#include <string_view>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

std::optional<AstNodeDescription>
DefaultAstNodeDescriptionProvider::createDescription(
    AstNode& node, std::string_view name, const Document* document) const {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return std::nullopt;

  if (name.empty()) {
    const std::optional<std::string_view> declared =
        services.nameProvider->getName(node);
    if (!declared.has_value()) return std::nullopt;
    name = *declared;
  }
  if (name.empty()) return std::nullopt;

  return AstNodeDescription{.name = name,
                            .type = node.type,
                            .documentId = document->id,
                            .symbolId = node.cstNodeId,
                            .node = &node};
}

}  // namespace cursiv
