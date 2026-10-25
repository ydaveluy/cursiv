#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cursiv/core/documentation/DocumentationProvider.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

// The documentation a hover shows: the comment the author wrote, rendered to
// markdown with its {@link} targets resolved. The element arm reads the
// grammar's own doc column. No per-document state, no cache to invalidate:
// every answer is read where it lives.

namespace cursiv {

// Not `final`: the four protected hooks are how a language changes what a
// link points at or how a tag renders, without rewriting the rendering.
class DefaultDocumentationProvider : public DocumentationProvider,
                                     protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::optional<std::string> getDocumentation(
      const AstNode& node, const Document* document = nullptr) const override;

 protected:
  // The markdown for one {@link name display}: resolved to the declaration's
  // name site as `[display](uri#L{line},{column})`, 1-based. Nullopt falls
  // back to the default rendering -- a URL stays a link, anything else is the
  // display text.
  [[nodiscard]] virtual std::optional<std::string> documentationLinkRenderer(
      const AstNode& node, const Document& document, std::string_view name,
      std::string_view display) const;
  // The markdown for one @tag; nullopt keeps the default rendering.
  [[nodiscard]] virtual std::optional<std::string> documentationTagRenderer(
      const AstNode& node, const Document& document, std::string_view name,
      std::string_view content, bool inlineTag) const;
  // `name` as a doc link reads it: the exact spelling, climbing the node's
  // containers -- no type filter, this is prose and not linking.
  [[nodiscard]] virtual std::optional<AstNodeDescription>
  findNameInLocalSymbols(const AstNode& node, const Document& document,
                         std::string_view name) const;
  // The exact spelling, first in the index's export order, any type.
  [[nodiscard]] virtual std::optional<AstNodeDescription> findNameInGlobalScope(
      const AstNode& node, const Document& document,
      std::string_view name) const;
};

}  // namespace cursiv
