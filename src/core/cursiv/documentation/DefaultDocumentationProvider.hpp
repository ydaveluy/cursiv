#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cursiv/documentation/DocumentationProvider.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

// The documentation a hover shows: the author's comment as markdown, with its
// {@link} targets resolved. It keeps no state.

namespace cursiv {

class DefaultDocumentationProvider : public DocumentationProvider,
                                     protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::optional<std::string> getDocumentation(
      const AstNode& node, const Document* document = nullptr) const override;

 protected:
  // The markdown for one {@link name display}, at the declaration's name:
  // `[display](uri#L{line},{column})`, 1-based. Nullopt uses the default.
  [[nodiscard]] virtual std::optional<std::string> documentationLinkRenderer(
      const AstNode& node, const Document& document, std::string_view name,
      std::string_view display) const;
  // The markdown for one @tag; nullopt keeps the default rendering.
  [[nodiscard]] virtual std::optional<std::string> documentationTagRenderer(
      const AstNode& node, const Document& document, std::string_view name,
      std::string_view content, bool inlineTag) const;
  // The exact spelling, in the local scopes of the node and its containers,
  // any type.
  [[nodiscard]] virtual std::optional<AstNodeDescription>
  findNameInLocalSymbols(const AstNode& node, const Document& document,
                         std::string_view name) const;
  // The exact spelling, first in the index's export order, any type.
  [[nodiscard]] virtual std::optional<AstNodeDescription> findNameInGlobalScope(
      const AstNode& node, const Document& document,
      std::string_view name) const;
};

}  // namespace cursiv
