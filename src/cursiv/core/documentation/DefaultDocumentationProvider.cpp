#include "cursiv/core/documentation/DefaultDocumentationProvider.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/core/documentation/CommentProvider.hpp"
#include "cursiv/core/documentation/DocComment.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/LocalSymbols.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

namespace cursiv {

std::optional<std::string> DefaultDocumentationProvider::getDocumentation(
    const AstNode& node, const Document* home) const {
  if (home == nullptr) home = node.getDocument();
  if (home == nullptr) return std::nullopt;
  const Document& document = *home;
  // The author's comment, rendered rather than raw: a doc comment above a
  // declaration reaches its reader as markdown, its tags rendered and its
  // {@link} targets resolved through the hooks below. Nothing else answers
  // this arm.
  const std::optional<std::string> written =
      services.commentProvider->getComment(node, &document);
  if (!written || !is_doc_comment(*written)) return std::nullopt;

  DocCommentRenderOptions options;
  options.renderLink = [this, &node, &document](std::string_view target,
                                                std::string_view display) {
    return documentationLinkRenderer(node, document, target, display);
  };
  options.renderTag = [this, &node, &document](std::string_view name,
                                               std::string_view content,
                                               bool inlineTag) {
    return documentationTagRenderer(node, document, name, content, inlineTag);
  };
  std::string markdown = parse_doc_comment(*written).toMarkdown(options);
  if (markdown.empty()) return std::nullopt;
  return markdown;
}

std::optional<std::string>
DefaultDocumentationProvider::documentationLinkRenderer(
    const AstNode& node, const Document& document, std::string_view name,
    std::string_view display) const {
  std::optional<AstNodeDescription> description =
      findNameInLocalSymbols(node, document, name);
  if (!description) description = findNameInGlobalScope(node, document, name);
  // The node shortcut stands only while the target's build does; without it
  // the link degrades to text rather than dereferencing a stale identity.
  if (!description || description->node == nullptr) return std::nullopt;

  const Document* target = &document;
  std::shared_ptr<Document> held;
  if (description->documentId != document.id) {
    held = services.shared.documents->getDocument(description->documentId);
    if (held == nullptr) return std::nullopt;
    target = held.get();
  }

  // The name site is the target language's to find: the link may cross into
  // a document another grammar owns.
  const CoreServices* const language =
      target->language != nullptr
          ? target->language
          : services.shared.serviceRegistry->findServices(
                target->uri, target->textDocument().languageId());
  if (language == nullptr) return std::nullopt;
  const std::optional<CstNodeView> site =
      language->nameProvider->getNameNode(*description->node, target);
  if (!site) return std::nullopt;

  // 1-based, lines and columns both.
  const Position at = target->textDocument().positionAt(site->begin());
  const std::string_view uri(target->uri);
  return std::format("[{}]({}#L{},{})", display, uri.substr(0, uri.find('#')),
                     at.line + 1, at.character + 1);
}

std::optional<std::string>
DefaultDocumentationProvider::documentationTagRenderer(const AstNode&,
                                                       const Document&,
                                                       std::string_view,
                                                       std::string_view,
                                                       bool) const {
  return std::nullopt;
}

std::optional<AstNodeDescription>
DefaultDocumentationProvider::findNameInLocalSymbols(
    const AstNode& node, const Document& document,
    std::string_view name) const {
  const std::uint64_t hash = LocalSymbols::hash_name(name);
  for (const AstNode* scope = &node; scope != nullptr && scope != &document;
       scope = scope->parent) {
    std::optional<AstNodeDescription> found;
    document.localSymbols.find(scope, name, hash)
        .for_each([&](const AstNodeDescription& entry) {
          if (entry.name != name) return true;
          found = entry;
          return false;
        });
    if (found) return found;
  }
  return std::nullopt;
}

std::optional<AstNodeDescription>
DefaultDocumentationProvider::findNameInGlobalScope(
    const AstNode&, const Document&, std::string_view name) const {
  // An empty AcceptSet accepts everything: a doc link names prose, not a
  // reference the reflection could narrow.
  const AstNodeDescription* const found =
      services.shared.indexManager->findByName(name, /*caseSensitive=*/true, {},
                                               *services.astReflection);
  if (found == nullptr) return std::nullopt;
  return *found;
}

}  // namespace cursiv
