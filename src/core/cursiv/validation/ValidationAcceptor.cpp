#include "cursiv/validation/ValidationAcceptor.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

ValidationAcceptor::ValidationAcceptor(Sink sink, const Document& document,
                                       std::string_view source) noexcept
    : _sink(sink),
      _document(&document),
      _cst((document.parseResult.value != nullptr) ? &document.parseResult.cst()
                                                   : nullptr),
      _text(document.textDocument().getText()),
      _source(source) {}

const Cst* ValidationAcceptor::cstOf(const AstNode& node) const noexcept {
  if (node.cstNodeId == kNoNode) return nullptr;
  const Document* const nodeDocument = node.getDocument();
  const Cst* const cst =
      nodeDocument != nullptr ? &nodeDocument->parseResult.cst() : _cst;
  assert(cst == nullptr || node.cstNodeId < cst->size());
  return cst;
}

TextRange ValidationAcceptor::spanOf(const AstNode& node) const noexcept {
  const Cst* const cst = cstOf(node);
  if (cst == nullptr) return TextRange{};
  const CstNode& at = (*cst)[node.cstNodeId];
  return TextRange{at.begin, at.end};
}

// The view is checked, not trusted: a name in the CST pool or a converter's
// memory is not a slice of the document, so only `data()` is compared. A
// translated document's AST points into the translated text.
TextRange ValidationAcceptor::spanOfName(std::string_view borrowed,
                                         const AstNode& node) const noexcept {
  const SourceTranslation* const translation =
      _cst != nullptr ? _cst->translation() : nullptr;
  const std::string_view text =
      translation != nullptr ? translation->text() : _text;
  const char* const begin = text.data();
  if (const char* const end = begin + text.size();
      borrowed.data() == nullptr || text.data() == nullptr ||
      std::less<const char*>{}(borrowed.data(), begin) ||
      std::less<const char*>{}(end, borrowed.data() + borrowed.size()))
    return spanOf(node);
  const auto at = static_cast<TextOffset>(borrowed.data() - begin);
  const TextRange span{at, static_cast<TextOffset>(at + borrowed.size())};
  return translation != nullptr ? translation->original(span) : span;
}

// Where a member was written. An anchor that missed is `nullopt`, and the
// builder keeps the range it had -- the node's own unless it was given one.
std::optional<TextRange> ValidationAcceptor::propertySpan(
    const AstNode& node, std::string_view member,
    std::size_t index) const noexcept {
  return anchorSpan(node, member, index, false);
}

std::optional<TextRange> ValidationAcceptor::keywordSpan(
    const AstNode& node, std::string_view keyword,
    std::size_t index) const noexcept {
  return anchorSpan(node, keyword, index, true);
}

std::optional<TextRange> ValidationAcceptor::anchorSpan(
    const AstNode& node, std::string_view name, std::size_t index,
    bool keyword) const noexcept {
  if (node.cstNodeId == kNoNode) return std::nullopt;
  // The node's own document, or the one validated for a node no document
  // holds yet; found once, and handed to the search.
  const Document* const nodeDocument = node.getDocument();
  const Document* const document =
      nodeDocument != nullptr ? nodeDocument
                              : (_cst != nullptr ? _document : nullptr);
  if (document == nullptr) return std::nullopt;
  if (const Cst* const cst = &document->parseResult.cst();
      _anchorCst != cst || _anchorNode != node.cstNodeId) {
    _anchors.clear();
    _anchorCst = cst;
    _anchorNode = node.cstNodeId;
  }
  auto found = std::ranges::find_if(_anchors, [&](const Anchor& anchor) {
    return anchor.keyword == keyword && anchor.name == name;
  });
  if (found == _anchors.end()) {
    // A search that cannot finish is no anchor: the check keeps the range it
    // has. The first walk of a tree may allocate.
    try {
      if (index == 0) {
        const auto at =
            keyword ? find_node_for_keyword(node, name, index, document)
                    : find_node_for_property(node, name, index, document);
        if (at) return TextRange{at.begin(), at.end()};
      } else {
        auto nodes = keyword ? find_nodes_for_keyword(node, name, document)
                             : find_nodes_for_property(node, name, document);
        _anchors.emplace_back(std::string(name), keyword, std::move(nodes));
        found = _anchors.end() - 1;
      }
    } catch (...) {
      return std::nullopt;
    }
  }
  if (found == _anchors.end() || found->nodes.empty()) return std::nullopt;
  const auto& at = found->nodes[std::min(index, found->nodes.size() - 1)];
  return TextRange{at.begin(), at.end()};
}

void ValidationAcceptor::accept(const AstNode& node,
                                DiagnosticSeverity severity,
                                std::string_view message,
                                std::string_view code) const {
  accept(spanOf(node), severity, message, code);
}

void ValidationAcceptor::accept(TextRange span, DiagnosticSeverity severity,
                                std::string_view message,
                                std::string_view code) const {
  Diagnostic diagnostic{span, severity, std::string(message), {}, {}};
  if (!code.empty()) diagnostic.code = DiagnosticCode(code);
  accept(std::move(diagnostic));
}

// Every builder emits here, the one place the source is set; a diagnostic that
// already has one (a hook forwarding another language's finding) keeps it.
void ValidationAcceptor::accept(Diagnostic&& diagnostic) const {
  try {
    if (diagnostic.source.empty()) diagnostic.source = _source;
    _sink(std::move(diagnostic));
  } catch (...) {
    if (_lost == nullptr) _lost = std::current_exception();
    throw;
  }
}

void ValidationAcceptor::throwIfDiagnosticLost() const {
  if (_lost != nullptr) std::rethrow_exception(_lost);
}

}  // namespace cursiv
