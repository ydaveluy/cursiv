#include "cursiv/workspace/Document.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv {

Document::Document(TextDocument textDocument, std::string uri)
    : uri(uri.empty() ? std::string(textDocument.uri()) : std::move(uri)),
      _textDocument(std::move(textDocument)) {
  // Its own tag, so the walk up proves the cast rather than assuming it.
  type = &ast_type_tag<Document>;
}

const Document* AstNode::getDocument() const noexcept {
  const AstNode* top = this;
  while (top->parent != nullptr) top = top->parent;
  // Its own tag proves the cast rather than assuming it.
  return ast_cast<Document>(top);
}

// The nodes first: a destructor may read a `string_view` of the text held here,
// and that text goes before the parse result would.
Document::~Document() { parseResult.destroy_nodes(); }

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

std::string_view Document::keep_parts(
    std::span<const std::string_view> parts) const {
  return parseResult.cst().keep_text(parts);
}

const TextDocument& Document::textDocument() const noexcept {
  return _textDocument;
}

// The empty result is made before anything changes: a refused allocation
// leaves the document as it was.
void Document::resetForRebuild() {
  ParseResult empty;
  state = DocumentState::Changed;
  parseResult = std::move(empty);
  localSymbols.clear();
  diagnostics.clear();
  ++buildRevision;
}

}  // namespace cursiv
