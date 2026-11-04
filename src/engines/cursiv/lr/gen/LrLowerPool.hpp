// The pool a lowering walk builds in: a body laid out as a tree of nodes, and
// the slots a production is spelled with.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LowerPool.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv {

// The walk lays a body out as a tree of these, and a production is decided over
// the whole body, since its optional positions distribute over one another.
// `end` is end of input at a position; `none` takes no slot.
enum class LrShape : std::uint8_t {
  sym,
  seq,
  alt,
  unordered,
  rep,
  scope,
  none,
  end
};

// The actions one column of one element owes, innermost first.
using LrActs = std::vector<std::uint16_t>;

using LrShapeId = std::size_t;

struct LrLowerNode {
  LrShape kind{};
  LrShapeId sym = 0;      // sym: symbol | seq,alt: kid base | rep,scope: kid
  std::size_t count = 0;  // seq,alt: kid count
  std::uint32_t min = 0, max = 0;
  // What a directive over this element writes, and the record it opens.
  LrActs row{};
  LrActs made{};
  // The hover text of this use of a token.
  std::string_view doc{};
  // The skip configuration the boundary before this element is read under:
  // the region it stands in, zero for the grammar's.
  std::uint8_t cfg = 0;
  // This element opens a region, so its first boundary is the caller's: the
  // caller's configuration plus one, zero where it opens none. Not a changed
  // `cfg`: a repeated element is at the head once.
  std::uint8_t lead = 0;
  // ...and closes it, so the boundary after it is the caller's too.
  std::uint8_t trail = 0;
  // unordered: the items the text may leave out, one bit each. Each stands in
  // its present form, and the group may end where only these are left.
  std::uint32_t unorderedOptional = 0;
};

// What one element is to the right-hand side it stands in, and the actions
// its record owes -- creations first, since a record is opened before it is
// filled.
struct LrLowerSlot {
  std::uint16_t sym = 0;
  bool optional = false;
  bool none = false;
  // End of input asserted here: no slot, a condition on the reduce.
  bool end = false;
  std::uint8_t cfg = 0;
  std::uint8_t lead = 0;
  std::uint8_t trail = 0;
  // A choice one branch of which is the end assertion alone: dropping the
  // slot is that branch.
  bool endAlt = false;
  LrActs made{};
  LrActs row{};
  std::string_view doc{};
};

inline LrActs lr_cat(const LrActs& a, const LrActs& b) {
  LrActs o = a;
  o.insert(o.end(), b.begin(), b.end());
  return o;
}

// What the walk holds while it runs, beside what both lowerings keep.
struct LrLowerPool : LowerPool {
  // The empty rule a condition is reduced by, plus one.
  std::vector<std::uint16_t> gapNt{};
  // ...and the same for a commit's exit the token in hand decides, by cut.
  std::unordered_map<std::uint64_t, std::uint16_t> stopCut{};
  std::vector<std::uint16_t> stopNt{};
  // ...and the rule of such an exit that ends a region, by cut and edge.
  std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint16_t> stopTail{};
  bool is_gap(std::uint16_t sym) const {
    return !is_term(sym) &&
           (std::ranges::find(gapNt, nt_index(sym) + 1u) != gapNt.end() ||
            stop_of(sym) != 0);
  }
  // The cut a symbol stops on, plus one; zero for any other.
  std::uint16_t stop_of(std::uint16_t sym) const {
    if (is_term(sym)) return 0;
    const auto at = std::ranges::find(stopNt, nt_index(sym) + 1u);
    return at == stopNt.end()
               ? std::uint16_t{0}
               : static_cast<std::uint16_t>(at - stopNt.begin() + 1);
  }
  // The section that rule is read in, its configuration; zero outside any.
  std::uint8_t home = 0;

  // A reading whose last boundary is another configuration's, past the region
  // it closes: by the reading it copies and that configuration, and back.
  // Minted on demand.
  std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint16_t> openTail{};
  std::unordered_map<std::uint16_t, std::pair<std::uint16_t, std::uint8_t>>
      openOf{};
  std::set<std::uint16_t> openDone{};

  std::vector<LrLowerNode> e{};
  std::vector<LrShapeId> kid{};
  std::unordered_map<LrShapeId, std::vector<LrShapeId>> unorderedHead{};
  std::unordered_map<LrShapeId, std::vector<LrShapeId>> unorderedTail{};

  // The kind of every write, by row id.
  std::vector<WriteKind> rowKind{};
  // Lowering a position whose turn reaches back.
  bool turning = false;
  // The rule's close, dropped from an alternative that is one rule call.
  std::uint16_t passClose = kNoRow;

  // Actions whose record wraps what stands before it: a nest's checkpoint,
  // the one position that writes a nest.
  bool turns(const LrActs& made) const {
    return !made.empty() && rowKind[made.front()] == WriteKind::nest;
  }
  // Flags run after their operand in the same construction context. Unlike
  // assign/append, they do not give that operand a separate current node.
  bool only_flags(const LrActs& rows) const {
    return std::ranges::all_of(rows, [&](std::uint16_t r) {
      return rowKind[r] == WriteKind::flag;
    });
  }
  // A position whose first reading is a nest turn.
  bool head_turns(LrShapeId at) const {
    const LrLowerNode& n = e[at];
    if (turns(n.made)) return true;
    if (!n.made.empty() || !only_flags(n.row)) return false;
    if (n.kind == LrShape::rep) return head_turns(n.sym);
    if (n.kind == LrShape::seq) {
      for (std::size_t i = 0; i < n.count; ++i) {
        const LrShapeId at = kid[n.sym + i];
        if (head_turns(at)) return true;
        const auto& before = e[at];
        // Conditions and flags do not replace a receiver. A creation or a
        // value write does; a token means this is no longer the head.
        if (!before.made.empty() || !only_flags(before.row) ||
            (before.kind != LrShape::none && before.kind != LrShape::end &&
             !(before.kind == LrShape::sym &&
               is_gap(static_cast<std::uint16_t>(before.sym)))))
          return false;
      }
      return false;
    }
    if (n.kind == LrShape::alt || n.kind == LrShape::unordered)
      for (std::size_t i = 0; i < n.count; ++i)
        if (head_turns(kid[n.sym + i])) return true;
    return false;
  }

  bool holds_turn(LrShapeId at) const {
    const LrLowerNode& n = e[at];
    for (const std::uint16_t r : n.made)
      if (rowKind[r] == WriteKind::nest) return true;
    if (n.kind == LrShape::rep || n.kind == LrShape::scope)
      return holds_turn(n.sym);
    if (n.kind == LrShape::seq || n.kind == LrShape::alt ||
        n.kind == LrShape::unordered)
      for (std::size_t i = 0; i < n.count; ++i)
        if (holds_turn(kid[n.sym + i])) return true;
    return false;
  }

  LrShapeId push(LrLowerNode x) {
    e.push_back(std::move(x));
    return e.size() - 1;
  }

  LrShapeId copy(LrShapeId at) {
    LrLowerNode n = e[at];
    if (n.kind == LrShape::rep || n.kind == LrShape::scope) {
      n.sym = copy(n.sym);
    } else if (n.kind == LrShape::seq || n.kind == LrShape::alt ||
               n.kind == LrShape::unordered) {
      std::vector<LrShapeId> fresh;
      fresh.reserve(n.count);
      for (std::size_t i = 0; i < n.count; ++i)
        fresh.push_back(copy(kid[n.sym + i]));
      n.sym = kid.size();
      kid.insert(kid.end(), fresh.begin(), fresh.end());
    }
    const LrShapeId fresh = push(std::move(n));
    if (const auto it = unorderedHead.find(at); it != unorderedHead.end())
      unorderedHead.try_emplace(fresh, it->second);
    if (const auto it = unorderedTail.find(at); it != unorderedTail.end())
      unorderedTail.try_emplace(fresh, it->second);
    return fresh;
  }
};

}  // namespace cursiv
