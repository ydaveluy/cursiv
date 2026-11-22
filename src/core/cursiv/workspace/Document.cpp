#include "cursiv/workspace/Document.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv {

std::uint64_t detail::next_build_revision() noexcept {
  static std::atomic<std::uint64_t> next{0};
  return next.fetch_add(1, std::memory_order_relaxed);
}

Document::Document(TextDocument textDocument, std::string uri)
    : uri(uri.empty() ? std::string(textDocument.uri()) : std::move(uri)),
      _textDocument(std::move(textDocument)) {
  // Its own tag, so the walk up proves the cast rather than assuming it.
  type = &kAstTypeTag<Document>;
}

const AstNode& rootOf(const AstNode& node) noexcept {
  const AstNode* root = &node;
  while (root->parent != nullptr && ast_cast<Document>(root->parent) == nullptr)
    root = root->parent;
  return *root;
}

const Document* AstNode::getDocument() const noexcept {
  const AstNode* top = this;
  while (top->parent != nullptr) top = top->parent;
  // Its own tag proves the cast rather than assuming it.
  return ast_cast<Document>(top);
}

// The nodes first: a destructor may read a `string_view` of the text held here,
// and that text goes before the parse result would.
Document::~Document() { parseResult.destroyNodes(); }

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

std::string_view Document::keepParts(
    std::span<const std::string_view> parts) const {
  return parseResult._tree->keepText(parts);
}

const TextDocument& Document::textDocument() const noexcept {
  return _textDocument;
}

void Document::reportBuildFailure(std::string_view message) {
  diagnostics.push_back({.message = std::string(message),
                         .source = "cursiv",
                         .code = DiagnosticCode("language-service-error")});
  buildFailed = true;
}

// Dropping a revision also drops any failure reported for its text.
void Document::resetForRebuild() {
  if (state.load() != DocumentState::Changed) {
    ParseResult empty;
    state = DocumentState::Changed;
    parseResult = std::move(empty);
    localSymbols.clear();
  }
  diagnostics.clear();
  buildFailed = false;
  buildRevision = detail::next_build_revision();
}

}  // namespace cursiv
