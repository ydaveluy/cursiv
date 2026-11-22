#include "cursiv/validation/ValidationAcceptor.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/utils/macros.hpp"
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

namespace {

// How far a diagnostic walks up to its node's root before it looks the node up
// among the validated document's nodes instead.
constexpr int kShortWalk = 16;

// A deep node: one of `validated`'s is found among its nodes by id, with no
// walk up to the root; any other by its root, or is `validated`'s.
CURSIV_COLD CURSIV_NOINLINE const Document* deep_document_of(
    const AstNode& node, const Document* validated) noexcept {
  if (validated != nullptr) {
    const OwnedNodes nodes = validated->parseResult.nodes();
    const auto twins = std::ranges::equal_range(nodes, node.cstNodeId, {},
                                                &AstNode::cstNodeId);
    if (std::ranges::find(twins, &node) != twins.end()) return validated;
  }
  const Document* const root = node.getDocument();
  return root != nullptr ? root : validated;
}

}  // namespace

// A node no document holds is the validated document's, when that one has a
// tree. A diagnostic never walks a deep chain up to the root.
const Document* ValidationAcceptor::documentOf(
    const AstNode& node) const noexcept {
  if (node.cstNodeId == kNoNode) return nullptr;
  const AstNode* top = &node;
  for (int step = 1; top->parent != nullptr; top = top->parent, ++step)
    if (step == kShortWalk)
      return deep_document_of(node, _cst != nullptr ? _document : nullptr);
  if (const Document* const root = ast_cast<Document>(top)) return root;
  return _cst != nullptr ? _document : nullptr;
}

TextRange ValidationAcceptor::spanOf(const AstNode& node) const noexcept {
  const Document* const document = documentOf(node);
  if (document == nullptr) return TextRange{};
  const Cst& cst = document->parseResult.cst();
  assert(node.cstNodeId < cst.size());
  const CstNode& at = cst[node.cstNodeId];
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
  // The node's document, found once and handed to the search.
  const Document* const document = documentOf(node);
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
            keyword ? findNodeForKeyword(node, name, index, document)
                    : findNodeForProperty(node, name, index, document);
        if (at) return TextRange{at.begin(), at.end()};
      } else {
        auto nodes = keyword ? findNodesForKeyword(node, name, document)
                             : findNodesForProperty(node, name, document);
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

void ValidationAcceptor::acceptBatches(
    std::vector<std::vector<Diagnostic>> batches) const {
  try {
    if (_runs != nullptr) {
      std::erase_if(batches, [](const auto& batch) { return batch.empty(); });
      if (_runs->empty())
        _runs->swap(batches);
      else
        _runs->insert(_runs->end(), std::make_move_iterator(batches.begin()),
                      std::make_move_iterator(batches.end()));
    } else {
      for (auto& batch : batches)
        for (auto& diagnostic : batch) _sink(std::move(diagnostic));
    }
  } catch (...) {
    if (_lost == nullptr) _lost = std::current_exception();
    throw;
  }
}

}  // namespace cursiv
