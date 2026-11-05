// Everything a thunk may touch while a tree is reduced: the tape and the
// layout its records are stamped in, the arena, the lists the result keeps, and
// the table the thunks read.
#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv {

struct AstActions;
namespace detail {
class AstArena;
struct TextValues;
}  // namespace detail

struct AstBuildCtx {
  const Cst* cst = nullptr;
  detail::AstArena* arena = nullptr;
  std::vector<RefHandle>* refs = nullptr;
  // What a reference will ask when it is read, planted at the write.
  const RefHome* home = nullptr;
  std::vector<ParseDiagnostic>* diags = nullptr;
  std::pmr::vector<AstNode*>* nodes = nullptr;
  const AstActions* actions = nullptr;
  AstBuildError error = AstBuildError::none;
  NodeId errorNode = kNoNode;
  // A tape no repair touched holds no invented record.
  bool repaired = false;
  bool detached = false;
  // How the tape's records name their row: a thunk serves every table.
  StampLayout stamp{};
  // Nodes a member took. The list holds one more, the root, unless a node was
  // built and dropped.
  std::size_t attached = 0;
  // Scoped to one text query; independent writes still run their converters.
  detail::TextValues* textValues = nullptr;
  // Nodes made and not yet on `nodes`, which takes them a run at a time: the
  // list's own append is a call where this is a store.
  static constexpr std::size_t kMadeRun = 256;
  std::size_t madeCount = 0;
  AstNode* made[kMadeRun]{};

  bool failed() const { return error != AstBuildError::none; }
  // Text a view of the tree names, which the tree keeps.
  std::string_view keep_text(std::string_view text) const {
    return cst->keep_text({&text, 1});
  }
  std::string_view keep_text(std::span<const std::string_view> parts) const {
    return cst->keep_text(parts);
  }
  // In creation order, which is the list's. A node stays here if the list
  // refuses it: the reducer's teardown answers for it.
  void list(AstNode* node) {
    made[madeCount] = node;
    if (++madeCount == kMadeRun) [[unlikely]]
      list_made();
  }
  // Whoever reads `nodes` calls this first.
  CURSIV_NOINLINE void list_made() {
    nodes->insert(nodes->end(), made, made + madeCount);
    madeCount = 0;
  }
  // First failure wins: the later ones are its consequences.
  void fail(AstBuildError why, NodeId id) {
    if (!failed()) {
      error = why;
      errorNode = id;
    }
  }
  // Collect and continue: an out-of-range literal must not cost an editor its
  // AST.
  void diag(std::string message, NodeId id) {
    diags->push_back(place({.kind = DiagnosticKind::conversion_error,
                            .message = std::move(message)},
                           id));
  }
  ParseDiagnostic place(ParseDiagnostic d, NodeId id) const {
    if (id != kNoNode && cst != nullptr) {
      d.begin = (*cst)[id].begin;
      d.end = (*cst)[id].end;
    }
    return d;
  }
};

}  // namespace cursiv
