#include "cursiv/core/syntax-tree/ParseResult.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"

// A result is installed once per parse and read afterwards: none of this is on
// a parse's path. It compiles once.

namespace cursiv {

ParseResult::ParseResult()
    : _cst(std::make_unique<Cst>()), nodes(_cst->memory()) {}

ParseResult::ParseResult(Cst&& tree)
    : _cst(std::make_unique<Cst>(std::move(tree))),
      nodes(_cst->memory()) {}

// Hand-written so the source reports itself empty, as an assignment leaves it:
// `= default` would keep `value` over a tree it no longer owns.
ParseResult::ParseResult(ParseResult&& src) noexcept
    : _grammarOwner(std::move(src._grammarOwner)),
      _cst(std::move(src._cst)),
      arena(std::move(src.arena)),
      value(std::exchange(src.value, nullptr)),
      references(std::move(src.references)),
      diagnostics(std::move(src.diagnostics)),
      error(src.error),
      accepted(src.accepted),
      nodes(std::move(src.nodes)) {}

ParseResult::~ParseResult() = default;

// Self-move must be a no-op, hence the guard.
ParseResult& ParseResult::operator=(ParseResult&& src) noexcept {
  if (this == &src) return *this;
  std::destroy_at(&nodes);  // on this pool, which goes with the tree
  _cst = std::move(src._cst);
  _grammarOwner = std::move(src._grammarOwner);
  arena = std::move(src.arena);
  value = src.value;
  references = std::move(src.references);
  diagnostics = std::move(src.diagnostics);
  error = src.error;
  accepted = src.accepted;
  // Constructed, not assigned: an assignment would allocate on the pool
  // given up above.
  std::construct_at(&nodes, std::move(src.nodes));
  src.value = nullptr;
  return *this;
}

const Cst& ParseResult::cst() const noexcept { return *_cst; }

void ParseResult::bind_grammar(const Grammar& grammar) noexcept {
  // The key is read off the nodes: written before the sort, never after.
  assert(std::is_sorted(nodes.begin(), nodes.end(),
                        [](const AstNode* a, const AstNode* b) {
                          return a->cstNodeId < b->cstNodeId;
                        }));
  CstBuilder(*_cst).bind_model(grammar, nodes);
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
