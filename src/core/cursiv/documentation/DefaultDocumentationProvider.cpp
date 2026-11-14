#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/documentation/DefaultDocumentationProvider.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/documentation/CommentProvider.hpp"
#include "cursiv/documentation/DocComment.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/text/Position.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/LocalSymbols.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv {

std::optional<std::string> DefaultDocumentationProvider::getDocumentation(
    const AstNode& node, const Document* nodeDocument) const {
  if (nodeDocument == nullptr) nodeDocument = node.getDocument();
  if (nodeDocument == nullptr) return std::nullopt;
  const Document& document = *nodeDocument;
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
  // `node` is set only while the target document is built; without it the
  // link stays plain text.
  if (!description || description->node == nullptr) return std::nullopt;

  const Document* target = &document;
  std::shared_ptr<Document> held;
  if (description->documentId != document.id) {
    held = shared.documents->getDocument(description->documentId);
    if (held == nullptr) return std::nullopt;
    target = held.get();
  }

  // The target document may belong to another language: ask its name provider.
  const CoreServices* const language = target->language;
  if (language == nullptr) return std::nullopt;
  const CstNodeView site =
      language->nameProvider->getNameNode(*description->node, target);
  if (!site) return std::nullopt;

  // 1-based, lines and columns both.
  const Position at = target->textDocument().positionAt(site.begin());
  const std::string_view uri(target->uri);
  return std::format("[{}]({})", display,
                     normalize_uri(std::format("{}#L{},{}",
                                               uri.substr(0, uri.find('#')),
                                               at.line + 1, at.character + 1)));
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
  const std::uint64_t hash = LocalSymbols::hashName(name);
  const LocalSymbols& symbols = document.localSymbols;
  std::optional<AstNodeDescription> found;
  symbols.walkScopes(&node, &document, [&](const AstNode* scope) {
    symbols.find(scope, name, hash)
        .forEach([&](const AstNodeDescription& entry) {
          if (entry.name != name) return true;
          found = entry;
          return false;
        });
    return found.has_value();
  });
  return found;
}

std::optional<AstNodeDescription>
DefaultDocumentationProvider::findNameInGlobalScope(
    const AstNode&, const Document&, std::string_view name) const {
  // An empty AcceptSet accepts every type.
  const AstNodeDescription* const found =
      shared.indexManager->findByName(name, /*caseSensitive=*/true, {},
                                               *services.astReflection);
  if (found == nullptr) return std::nullopt;
  return *found;
}

}  // namespace cursiv
