#pragma once

#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"

// What one parse left behind: the CST and the AST nodes its pool holds, the
// references to link, and what the repairs had to do.

namespace cursiv {

struct Grammar;
class ParseResultBuilder;

class ParseResult {
 public:
  struct PropertyOrigin {
    NodeId record;
    const AstNode* owner;
  };

 private:
  // The tree, where the nodes live on its pool, and every node the builder
  // created, sorted by `cstNodeId`. Held by pointer so the result can move;
  // member order is teardown order: the tree outlives what lives on its
  // pool.
  struct Tree {
    explicit Tree(Cst&& tree) : cst(std::move(tree)), nodes(cst._pool.get()) {}
    Cst cst;
    std::pmr::vector<AstNode*> nodes;
    std::vector<PropertyOrigin> origins;

    struct Joined {
      std::span<char> storage;
      std::string_view text;
    };

    // Successive joins may extend the last view into unused space on either side.
    std::string_view keepText(std::span<const std::string_view> parts, Joined* last = nullptr) const;
  };
  std::shared_ptr<const Grammar> _grammarOwner;
  std::unique_ptr<Tree> _tree;  // destroyed before its grammar
  friend struct Document;
  void destroyNodes() noexcept;
  friend class ParseResultBuilder;

 public:
  ParseResult();
  explicit ParseResult(Cst&& tree);
  ParseResult(ParseResult&&) noexcept;
  ~ParseResult();

  // The map dies before the tree on assignment too: the defaulted operator
  // would free the pool it lives on first.
  ParseResult& operator=(ParseResult&& src) noexcept;

  // Const only: `Cst::reset` on a mutable tree would free the pool the AST
  // nodes live on.
  [[nodiscard]] const Cst& cst() const noexcept;
  // Called by the language parser once the build is over, so readers get the
  // grammar from the tree.
  void bindGrammar(const Grammar& grammar) noexcept;
  void bindGrammar(std::shared_ptr<const Grammar> grammar) noexcept;
  // Called by a translating parser: the tree and diagnostics move to the
  // document's offsets, and the translator's errors join them. A failure
  // leaves the result fit only to be destroyed.
  void bindTranslation(std::string_view document,
                       std::unique_ptr<SourceTranslation> translation);

  AstNode* value = nullptr;
  // What answers the tree's references, null where its language has none.
  // Held by pointer: this result moves, and every reference stores its address.
  std::unique_ptr<DocumentReferences> references;
  // What the repairs said, then what the conversions added, in one list.
  std::vector<ParseDiagnostic> diagnostics;
  AstBuildError error = AstBuildError::None;
  bool accepted = false;

  explicit operator bool() const noexcept;

  // The root as a `T`, null when it is not one.
  template <class T>
  T* valueAs() const {
    return const_cast<T*>(ast_cast<T>(value));
  }

  // Former records that directly filled an adopted node, in record order.
  [[nodiscard]] std::span<const PropertyOrigin> propertyOrigins() const noexcept {
    if (!_tree) return {};
    return _tree->origins;
  }

  // Every node the builder created, sorted by `cstNodeId`: postorder, so the
  // first hit containing an offset is the innermost. An id is not a key: a
  // wrapper shares its content's record.
  [[nodiscard]] OwnedNodes nodes() const noexcept {
    return _tree != nullptr ? OwnedNodes(_tree->nodes) : OwnedNodes();
  }
};

}  // namespace cursiv
