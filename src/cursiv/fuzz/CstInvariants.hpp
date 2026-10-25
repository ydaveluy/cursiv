// Checks properties a postorder CST over a text cannot fail to have, whatever
// grammar or engine produced it -- spans inside the text, subtrees inside the
// tape, children inside their parent and in order. Nothing here reads a table;
// the AST is judged separately, by `AstCompare`.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv::fuzzer {

struct CstInvariants {
  struct Result {
    bool ok = true;
    std::string why;
    cursiv::NodeId at = cursiv::kNoNode;
  };

  [[nodiscard]] Result run(const cursiv::Cst& c, std::string_view text) const {
    const cursiv::NodeCount n = c.size();
    for (cursiv::NodeId id = 0; id < n; ++id) {
      const cursiv::CstNode& x = c[id];
      if (x.begin > x.end || x.end > text.size())
        return fail(id, "a record's span leaves the text");
      // Postorder is the invariant: a subtree is the run ending at its
      // root, so its extent cannot reach past the tape's own start.
      if (x.subtreeSize == 0 || x.subtreeSize > id + 1)
        return fail(id, "a record's subtree reaches past the tape");

      // Children are walked backwards: a subtree ends at its root, so the
      // last child is `id - 1` and each earlier one ends where that child's
      // subtree begins. Their spans must not overlap or leave the parent's.
      const cursiv::NodeId low = c.subtree_begin(id);
      if (id == low) continue;
      cursiv::TextOffset after = x.end;
      cursiv::TextOffset first = x.end;
      for (cursiv::NodeId k = id; k > low;) {
        const cursiv::NodeId child = k - 1;
        const cursiv::CstNode& kid = c[child];
        if (kid.begin < x.begin || kid.end > x.end)
          return fail(child, "a child's span leaves its parent's");
        if (kid.end > after)
          return fail(child, "a child ends after the one behind it begins");
        after = first = kid.begin;
        k = c.subtree_begin(child);
      }
      if (first != x.begin)
        return fail(id, "a record reaches in front of its first child");
    }
    return Result{};
  }

  static Result fail(cursiv::NodeId id, const char* why) {
    return Result{false, why, id};
  }
};

}  // namespace cursiv::fuzzer
