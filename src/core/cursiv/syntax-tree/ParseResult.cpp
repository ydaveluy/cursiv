#include "cursiv/syntax-tree/ParseResult.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

namespace cursiv {

std::string_view ParseResult::Tree::keepText(std::span<const std::string_view> parts, Joined* last) const {
  std::size_t size = 0, prefix = 0;
  auto middle = parts.size();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto part = parts[i];
    if (part.size() > std::numeric_limits<std::size_t>::max() - size) throw std::bad_alloc();
    if (last && !part.empty() && part.data() == last->text.data() && part.size() == last->text.size()) {
      middle = i;
      prefix = size;
    }
    size += part.size();
  }
  const auto copy = [](char* dest, std::span<const std::string_view> from) {
    for (const auto part : from) {
      if (!part.empty()) std::char_traits<char>::copy(dest, part.data(), part.size());
      dest += part.size();
    }
  };
  std::size_t spare = 0;
  if (middle < parts.size()) {
    const auto suffix = size - prefix - last->text.size();
    const auto storage = last->storage;
    const auto left = static_cast<std::size_t>(last->text.data() - storage.data());
    if (prefix <= left && suffix <= storage.size() - left - last->text.size()) {
      char* const begin = storage.data() + left - prefix;
      copy(begin, parts.first(middle));
      copy(begin + prefix + last->text.size(), parts.subspan(middle + 1));
      return last->text = {begin, size};
    }
    if (size <= std::numeric_limits<std::size_t>::max() / 2) spare = size;
  }
  const auto capacity = std::max<std::size_t>(1, size + spare);
  char* const storage = static_cast<char*>(cst._pool->allocate(capacity, alignof(char)));
  char* const begin = storage + spare / 2;
  copy(begin, parts);
  const std::string_view text(begin, size);
  if (last) *last = {{storage, capacity}, text};
  return text;
}

ParseResult::ParseResult() : ParseResult(Cst()) {}

ParseResult::ParseResult(Cst&& tree) : _tree(std::make_unique<Tree>(std::move(tree))) {}

// Hand-written so the source reports itself empty, as an assignment leaves it:
// `= default` would keep `value` over a tree it gave away.
ParseResult::ParseResult(ParseResult&& src) noexcept { *this = std::move(src); }

ParseResult::~ParseResult() { destroyNodes(); }

// No order is needed: a node's destructor frees what its members own, and a
// child is a plain pointer, so this never recurses. The nodes' memory stays
// with the pool.
void ParseResult::destroyNodes() noexcept {
  if (_tree == nullptr) return;
  for (AstNode* const node : _tree->nodes) node->~AstNode();
  _tree->nodes.clear();
}

ParseResult& ParseResult::operator=(ParseResult&& src) noexcept {
  if (this == &src) return *this;
  destroyNodes();
  _tree = std::move(src._tree);
  _grammarOwner = std::move(src._grammarOwner);
  value = src.value;
  references = std::move(src.references);
  diagnostics = std::move(src.diagnostics);
  error = src.error;
  accepted = src.accepted;
  src.value = nullptr;
  return *this;
}

const Cst& ParseResult::cst() const noexcept { return _tree->cst; }

void ParseResult::bindGrammar(const Grammar& grammar) noexcept {
  // The key is read off the nodes: written before the sort, never after.
  assert(std::is_sorted(_tree->nodes.begin(), _tree->nodes.end(),
                        [](const AstNode* a, const AstNode* b) {
                          return a->cstNodeId < b->cstNodeId;
                        }));
  CstBuilder(_tree->cst).bindModel(grammar, _tree->nodes);
}

void ParseResult::bindGrammar(std::shared_ptr<const Grammar> grammar) noexcept {
  _grammarOwner = std::move(grammar);
  bindGrammar(*_grammarOwner);
}

// The tree takes the text first: its nodes point into it.
void ParseResult::bindTranslation(
    std::string_view document, std::unique_ptr<SourceTranslation> translation) {
  assert(_tree->cst.translation() == nullptr && "a result binds one translation");
  const SourceTranslation& bound = *translation;
  CstBuilder(_tree->cst).bindTranslation(document, std::move(translation));
  bound.toDocument(std::span(diagnostics));
  const auto errors = bound.errors();
  diagnostics.insert(diagnostics.end(), errors.begin(), errors.end());
}

ParseResult::operator bool() const noexcept {
  return accepted && value != nullptr && error == AstBuildError::None;
}

}  // namespace cursiv
