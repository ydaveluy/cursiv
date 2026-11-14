// Checks what any postorder CST over a text must hold, whatever produced it:
// spans inside the text with ends that never go back, subtrees inside the
// tape, children inside their parent and in order.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::fuzzer::detail {

struct CstInvariants {
  struct Result {
    bool ok = true;
    std::string why;
    cursiv::NodeId at = cursiv::kNoNode;
  };

  [[nodiscard]] Result run(const cursiv::Cst& c, std::string_view text) const {
    const cursiv::NodeCount n = c.size();
    cursiv::TextOffset lastEnd = 0;
    for (cursiv::NodeId id = 0; id < n; ++id) {
      const cursiv::CstNode& x = c[id];
      if (x.begin > x.end || x.end > text.size())
        return fail(id, "a record's span leaves the text");
      if (x.end < lastEnd)
        return fail(id, "a record ends before the one behind it");
      lastEnd = x.end;
      // Postorder is the invariant: a subtree is the run ending at its
      // root, so its extent cannot reach past the tape's own start.
      if (x.subtreeSize == 0 || x.subtreeSize > id + 1)
        return fail(id, "a record's subtree reaches past the tape");

      // Children backwards, from `id - 1`: their subtrees must tile the
      // parent's, and their spans must stay inside it without overlapping.
      const cursiv::NodeId low = c.subtreeBegin(id);
      if (id == low) continue;
      cursiv::TextOffset after = x.end;
      cursiv::TextOffset first = x.end;
      for (cursiv::NodeId k = id; k > low;) {
        const cursiv::NodeId child = k - 1;
        const cursiv::CstNode& childNode = c[child];
        if (c.subtreeBegin(child) < low)
          return fail(child, "a child's subtree leaves its parent's");
        if (childNode.begin < x.begin || childNode.end > x.end)
          return fail(child, "a child's span leaves its parent's");
        if (childNode.end > after)
          return fail(child, "a child ends after the one behind it begins");
        after = first = childNode.begin;
        k = c.subtreeBegin(child);
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

}  // namespace cursiv::fuzzer::detail
