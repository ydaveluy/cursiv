#include "cursiv/core/validation/ValidationAcceptor.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/SourceTranslation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

ValidationAcceptor::ValidationAcceptor(Sink sink, const Document& document,
                                       std::string_view source) noexcept
    : _sink(sink),
      _cst((document.parseResult.value != nullptr) ? &document.parseResult.cst()
                                                   : nullptr),
      _text(document.textDocument().getText()),
      _source(source) {}

const Cst* ValidationAcceptor::cstOf(const AstNode& node) const noexcept {
  if (node.cstNodeId == kNoNode) return nullptr;
  const Document* const home = node.getDocument();
  const Cst* const cst = home != nullptr ? &home->parseResult.cst() : _cst;
  assert(cst == nullptr || node.cstNodeId < cst->size());
  return cst;
}

TextRange ValidationAcceptor::spanOf(const AstNode& node) const noexcept {
  const Cst* const cst = cstOf(node);
  if (cst == nullptr) return TextRange{};
  const CstNode& at = (*cst)[node.cstNodeId];
  return TextRange{at.begin, at.end};
}

// The view is checked, not trusted: a name that lives in the CST's own pool --
// or in whatever a converter allocated -- is not a slice of the document, and
// subtracting the two pointers would be undefined and wrong. The comparison is
// on `data()`, which is the one question that can be asked of two unrelated
// buffers without arithmetic between them. A translated document's AST
// borrows the translated text, whose spans map back to the document.
TextRange ValidationAcceptor::spanOfName(std::string_view borrowed,
                                         const AstNode& node) const noexcept {
  const SourceTranslation* const translation =
      _cst != nullptr ? _cst->translation() : nullptr;
  const std::string_view text =
      translation != nullptr ? translation->text() : _text;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  if (borrowed.data() == nullptr || text.data() == nullptr ||
      std::less<const char*>{}(borrowed.data(), begin) ||
      std::less<const char*>{}(end, borrowed.data() + borrowed.size()))
    return spanOf(node);
  const auto at = static_cast<TextOffset>(borrowed.data() - begin);
  const TextRange span{at, static_cast<TextOffset>(at + borrowed.size())};
  return translation != nullptr ? translation->original(span) : span;
}

// Where a member was written; an anchor that missed still points at the
// declaration rather than at nothing.
std::optional<TextRange> ValidationAcceptor::featureSpan(
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
  const Cst* const cst = cstOf(node);
  if (cst == nullptr) return std::nullopt;
  if (_anchorCst != cst || _anchorNode != node.cstNodeId) {
    _anchors.clear();
    _anchorCst = cst;
    _anchorNode = node.cstNodeId;
  }
  auto found = std::ranges::find_if(_anchors, [&](const Anchor& anchor) {
    return anchor.keyword == keyword && anchor.name == name;
  });
  if (found == _anchors.end()) {
    const CstNodeView view{cst, node.cstNodeId};
    if (index == 0) {
      const auto at = keyword ? find_node_for_keyword(view, name, index)
                              : find_node_for_feature(view, name, index);
      if (at.has_value()) return TextRange{at->begin(), at->end()};
    } else {
      auto nodes = keyword ? find_nodes_for_keyword(view, name)
                           : find_nodes_for_feature(view, name);
      _anchors.push_back({std::string(name), keyword, std::move(nodes)});
      found = _anchors.end() - 1;
    }
  }
  if (found != _anchors.end() && !found->nodes.empty()) {
    const auto& at = found->nodes[std::min(index, found->nodes.size() - 1)];
    return TextRange{at.begin(), at.end()};
  }
  const CstNode& n = (*cst)[node.cstNodeId];
  return TextRange{n.begin, n.end};
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

// The funnel: every builder emits here, and the source is stamped in exactly
// one place -- a diagnostic that already carries one (a hook forwarding
// another language's finding) keeps it.
void ValidationAcceptor::accept(Diagnostic&& diagnostic) const {
  if (diagnostic.source.empty()) diagnostic.source = _source;
  _sink(std::move(diagnostic));
}

}  // namespace cursiv
