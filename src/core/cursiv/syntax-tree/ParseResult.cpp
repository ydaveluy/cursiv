#include "cursiv/syntax-tree/ParseResult.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

// A result is installed once per parse and read afterwards: none of this is on
// a parse's path. It compiles once.

namespace cursiv {

ParseResult::ParseResult()
    : _cst(std::make_unique<Cst>()),
      _arena(_cst->memory()),
      _nodes(_cst->memory()) {}

ParseResult::ParseResult(Cst&& tree)
    : _cst(std::make_unique<Cst>(std::move(tree))),
      _arena(_cst->memory()),
      _nodes(_cst->memory()) {}

// Hand-written so the source reports itself empty, as an assignment leaves it:
// `= default` would keep `value` over a tree it no longer owns.
ParseResult::ParseResult(ParseResult&& src) noexcept { *this = std::move(src); }

ParseResult::~ParseResult() { destroy_nodes(); }

void ParseResult::destroy_detached(std::span<AstNode* const> gone) noexcept {
  for (AstNode* const node : gone) node->~AstNode();
}

// No order is owed: a node's destructor frees what its members own, and a child
// is a plain pointer. So no depth of tree recurses here.
void ParseResult::destroy_nodes() noexcept {
  destroy_detached(_nodes);
  _nodes.clear();
}

// Self-move must be a no-op, hence the guard.
ParseResult& ParseResult::operator=(ParseResult&& src) noexcept {
  if (this == &src) return *this;
  destroy_nodes();
  std::destroy_at(&_nodes);  // on this pool, which goes with the tree
  _cst = std::move(src._cst);
  _grammarOwner = std::move(src._grammarOwner);
  _arena = std::move(src._arena);
  value = src.value;
  references = std::move(src.references);
  diagnostics = std::move(src.diagnostics);
  error = src.error;
  accepted = src.accepted;
  // Constructed, not assigned: an assignment would allocate on the pool
  // given up above.
  std::construct_at(&_nodes, std::move(src._nodes));
  src.value = nullptr;
  return *this;
}

const Cst& ParseResult::cst() const noexcept { return *_cst; }

void ParseResult::bind_grammar(const Grammar& grammar) noexcept {
  // The key is read off the nodes: written before the sort, never after.
  assert(std::is_sorted(_nodes.begin(), _nodes.end(),
                        [](const AstNode* a, const AstNode* b) {
                          return a->cstNodeId < b->cstNodeId;
                        }));
  CstBuilder(*_cst).bind_model(grammar, _nodes);
}

void ParseResult::bind_grammar(std::shared_ptr<const Grammar> grammar) noexcept {
  _grammarOwner = std::move(grammar);
  bind_grammar(*_grammarOwner);
}

void ParseResult::bind_translation(
    std::string_view document,
    std::unique_ptr<const SourceTranslation> translation) {
  translation->to_document(std::span(diagnostics));
  const auto errors = translation->errors();
  diagnostics.insert(diagnostics.end(), errors.begin(), errors.end());
  CstBuilder(*_cst).bind_translation(document, std::move(translation));
}

ParseResult::operator bool() const noexcept {
  return accepted && value != nullptr && error == AstBuildError::none;
}

}  // namespace cursiv
