// The structural laws a run states about itself, asked of one parse. `say`
// receives one sentence per breach; the tree is walked whole, so one call
// reports every one of them.
#pragma once

#include <cstdio>
#include <string_view>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"

namespace cursiv::test {

template <class Say>
void audit_cst_structure(const ParseResult& r, std::string_view text,
                         Say&& say) {
  const Cst& cst = r.cst();
  const NodeCount n = cst.size();
  char b[256];

  TextOffset lastEnd = 0;
  for (NodeId i = 0; i < n; ++i) {
    const CstNode& x = cst[i];
    if (x.begin > x.end || x.end > text.size()) {
      std::snprintf(b, sizeof b, "A1 node %u span [%u,%u) outside text (%zu)",
                    i, x.begin, x.end, text.size());
      say(b);
      continue;
    }
    if (x.end < lastEnd) {
      std::snprintf(b, sizeof b, "A2 node %u end %u decreases (previous %u)", i,
                    x.end, lastEnd);
      say(b);
    }
    lastEnd = x.end > lastEnd ? x.end : lastEnd;
    if (x.subtreeSize == 0 || x.subtreeSize > i + 1) {
      std::snprintf(b, sizeof b, "A3 node %u subtreeSize %u out of range", i,
                    x.subtreeSize);
      say(b);
      continue;
    }
    // The children, walked right to left: ordered, disjoint, inside.
    if (x.subtreeSize == 1) continue;
    NodeCount left = x.subtreeSize - 1;
    NodeId cur = i - 1;
    TextOffset lo = x.end;  // the previous (righter) child's begin
    bool broke = false;
    while (left > 0) {
      const CstNode& c = cst[cur];
      if (c.subtreeSize == 0 || c.subtreeSize > left) {
        std::snprintf(b, sizeof b, "A5 node %u child chain broken at %u", i,
                      cur);
        say(b);
        broke = true;
        break;
      }
      if (c.begin < x.begin || c.end > x.end) {
        std::snprintf(b, sizeof b,
                      "A6 child %u [%u,%u) escapes parent %u "
                      "[%u,%u)",
                      cur, c.begin, c.end, i, x.begin, x.end);
        say(b);
      }
      if (c.end > lo) {
        std::snprintf(b, sizeof b,
                      "A7 child %u [%u,%u) overlaps its right sibling under %u "
                      "(next begin %u)",
                      cur, c.begin, c.end, i, lo);
        say(b);
      }
      lo = c.begin;
      left -= c.subtreeSize;
      if (c.subtreeSize > cur) break;
      cur -= c.subtreeSize;
    }
    if (!broke && left != 0) {
      std::snprintf(b, sizeof b, "A8 node %u children do not sum to %u", i,
                    x.subtreeSize);
      say(b);
    }
  }

  // Leaves tile the text: in run order, non-overlapping and never going back.
  TextOffset cursor = 0;
  for (NodeId i = 0; i < n; ++i) {
    const CstNode& x = cst[i];
    if (x.subtreeSize != 1 || x.begin == x.end) continue;
    if (x.begin < cursor) {
      std::snprintf(b, sizeof b,
                    "A9 leaf %u [%u,%u) overlaps what a leaf took (cursor %u)",
                    i, x.begin, x.end, cursor);
      say(b);
    }
    cursor = x.end;
  }

  // Trivia posted twice: two hidden leaves over one span.
  for (NodeId i = 0; i < n; ++i) {
    if (!cst[i].hidden || cst[i].begin == cst[i].end) continue;
    for (NodeId j = i + 1; j < n; ++j)
      if (cst[j].hidden && cst[j].begin == cst[i].begin &&
          cst[j].end == cst[i].end) {
        std::snprintf(b, sizeof b, "A10 hidden %u and %u both hold [%u,%u)", i,
                      j, cst[i].begin, cst[i].end);
        say(b);
      }
  }

  if (!r.accepted) return;

  // An accepted document is covered by its root.
  if (!cst.has_entry()) {
    say("A11 accepted without an entry root");
    return;
  }
  const NodeId root = cst.entry_root();
  if (root >= n) {
    std::snprintf(b, sizeof b, "A12 entry root %u past the run (%u)", root, n);
    say(b);
    return;
  }

  // A tree, not a forest: what the entry root does not contain is trivia or a
  // repair's leftovers, which is what the bind excludes. A record of the
  // derivation standing outside it is a document half of which the tree does
  // not describe.
  const NodeId lo = root + 1 - cst[root].subtreeSize;
  for (NodeId i = 0; i < n; ++i) {
    if (i >= lo && i <= root) continue;
    const CstNode& x = cst[i];
    if (x.hidden || (x.recovered && x.subtreeSize == 1)) continue;
    std::snprintf(b, sizeof b,
                  "A13 record %u [%u,%u) sub=%u stands outside the entry root "
                  "%u [%u,%u)",
                  i, x.begin, x.end, x.subtreeSize, root, cst[root].begin,
                  cst[root].end);
    say(b);
    break;
  }

  // The contract every reader of a module holds: an accepted document has a
  // tree of its own.
  if (r.value == nullptr) say("A14 accepted, and no AST was built");
}

}  // namespace cursiv::test
