#include "cursiv/core/workspace/Document.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

// A document answers about a build that has already run: nothing here is on a
// parse's path. It compiles once.

namespace cursiv {

Document::Document(TextDocument textDocument, std::string uri)
    : uri(uri.empty() ? std::string(textDocument.uri()) : std::move(uri)),
      _textDocument(std::move(textDocument)) {
  // Its own tag, so the climb below proves the cast rather than assuming it.
  type = &ast_type_tag<Document>;
}

const Document* AstNode::getDocument() const noexcept {
  const AstNode* top = this;
  while (top->parent != nullptr) top = top->parent;
  // Its own tag proves the cast rather than assuming it.
  return top->type == &ast_type_tag<Document>
             ? static_cast<const Document*>(top)
             : nullptr;
}

Document::~Document() = default;

bool Document::parseSucceeded() const noexcept { return parseResult.accepted; }

bool Document::hasAst() const noexcept { return parseResult.value != nullptr; }

bool Document::parseRecovered() const noexcept {
  return std::ranges::any_of(
      parseResult.diagnostics,
      [](const ParseDiagnostic& d) { return d.isSyntax(); });
}

bool Document::hasLinkingErrors() const noexcept {
  const DocumentReferences* const references = parseResult.references.get();
  return references != nullptr && references->anyUnresolved();
}

const TextDocument& Document::textDocument() const noexcept {
  return _textDocument;
}

void Document::resetForRebuild() noexcept {
  state = DocumentState::Changed;
  parseResult = ParseResult{};
  localSymbols.clear();
  diagnostics.clear();
  ++buildRevision;
}

}  // namespace cursiv
