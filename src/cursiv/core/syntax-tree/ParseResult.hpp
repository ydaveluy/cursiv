#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"

// What one parse left behind: the two trees, the arena that owns the AST, the
// references to link, and what the repairs had to do. Not a template -- the
// builder is, the result is not, so a tool knows one result type.

namespace cursiv {

struct Grammar;

// The tree is held by pointer so the result can move: nothing the result
// owns stores a `Cst` address, but everything a caller derives from `cst()`
// does. Member order is teardown order: the tree outlives every member that
// lives on its pool.
class ParseResult {
  std::shared_ptr<const Grammar> _grammarOwner;
  std::unique_ptr<Cst> _cst;  // destroyed after the arena, before its grammar

 public:
  ParseResult();
  explicit ParseResult(Cst&& tree);
  ParseResult(ParseResult&&) noexcept;
  ~ParseResult();

  // The map dies before the tree on assignment too: the defaulted operator
  // would free the pool it lives on first.
  ParseResult& operator=(ParseResult&& src) noexcept;

  // Const only, and there is no other door: `Cst::reset` takes a mutable
  // `Cst&`, so handing one out would make a use-after-free spellable in one
  // line -- it returns the pool's buffers while the arena still carves from
  // them.
  [[nodiscard]] const Cst& cst() const noexcept;
  // Said by the language parser once the build is over: the tree then names
  // its grammar and its map, and no reader fetches either from a service.
  void bind_grammar(const Grammar& grammar) noexcept;
  void bind_grammar(std::shared_ptr<const Grammar> grammar) noexcept;
  // Said by a translating parser over the result of its text: the tree and
  // the diagnostics move to the document's offsets, and the translator's own
  // errors join them.
  void bind_translation(std::string_view document,
                        std::unique_ptr<const SourceTranslation> translation);

  AstArena arena;
  AstNode* value = nullptr;
  // Held by pointer: this result moves, and every reference of the tree
  // stores the address of what answers it.
  std::unique_ptr<DocumentReferences> references;
  // What the repairs said and what the conversions added, in that order:
  // one list -- who first noticed a problem is not a distinction a consumer
  // routes on.
  std::vector<ParseDiagnostic> diagnostics;
  AstBuildError error = AstBuildError::none;
  bool accepted = false;

  explicit operator bool() const noexcept;

  // The entry type is a static fact of the grammar, so no runtime test guards
  // this cast.
  template <class T>
  T* value_as() const {
    return static_cast<T*>(value);
  }

  // Every node the builder created, sorted by `cstNodeId` -- which is
  // postorder, so a parent always sits after its children and the first hit
  // containing an offset is the innermost. `lower_bound` on the id turns a
  // description back into a node without walking the AST. An id is not a key:
  // `wrap` builds its wrapper at the record its content already sits on.
  // On the tree's own pool, beside the records it indexes.
  std::pmr::vector<AstNode*> nodes;
};

}  // namespace cursiv
