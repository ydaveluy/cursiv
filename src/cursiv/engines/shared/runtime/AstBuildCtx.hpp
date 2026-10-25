// Everything a thunk may touch while a tree is reduced: the tape and the
// layout its records are stamped in, the arena, the lists the result keeps,
// and the table the thunks read.
#pragma once

#include <cstddef>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

class AstArena;
struct AstActions;

struct AstBuildCtx {
  const Cst* cst = nullptr;
  AstArena* arena = nullptr;
  std::pmr::memory_resource* mem = nullptr;
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

  bool failed() const { return error != AstBuildError::none; }
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
