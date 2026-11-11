// The pool a lowering walk builds in: a body stored as a tree of nodes, and
// the slots a production is written with.
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
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

// The walk arranges a body as a tree of these, and a production is decided over
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

using LrShapeId = std::size_t;

struct LrLowerNode {
  LrShape kind{};
  LrShapeId sym = 0;  // sym: symbol | seq,alt: child base | rep,scope: child
  std::size_t count = 0;  // seq,alt: child count
  std::uint32_t min = 0, max = 0;
  // What a directive over this element writes, and the record it opens.
  Acts row{};
  Acts made{};
  // The hover text of this use of a token.
  std::string_view doc{};
  // The skip configuration the boundary before this element is read under:
  // the region it stands in, zero for the grammar's.
  std::uint8_t cfg = 0;
  // This element opens a region, so its first boundary is the caller's: the
  // caller's configuration plus one, zero where it opens none. Not a changed
  // `cfg`: a repeated element is at the head once.
  std::uint8_t leading = 0;
  // This element closes a region, so the boundary after it is the caller's.
  std::uint8_t trailing = 0;
  // unordered: the items the text may leave out, one bit each. Each stands in
  // its present form, and the group may end where only these are left.
  std::uint32_t unorderedOptional = 0;
  // unordered: a nest stands under the group.
  bool unorderedTurns = false;
};

// What one element is to the right-hand side it stands in, and the actions
// its record needs, creations first, since a record is opened before it is
// filled.
struct LrLowerSlot {
  std::uint16_t sym = 0;
  bool optional = false;
  bool none = false;
  // End of input asserted here: no slot, a condition on the reduce.
  bool end = false;
  std::uint8_t cfg = 0;
  std::uint8_t leading = 0;
  std::uint8_t trailing = 0;
  // A choice one branch of which is the end assertion alone: dropping the
  // slot is that branch.
  bool endAlt = false;
  Acts made{};
  Acts row{};
  std::string_view doc{};
};

// What the walk holds while it runs, beside what both lowerings keep.
struct LrLowerPool : LowerPool {
  // The empty rule a condition is reduced by, plus one.
  std::vector<std::uint16_t> gapNt{};
  // The same for a commit's exit the token in hand decides, by cut.
  std::unordered_map<std::uint64_t, std::uint16_t> stopCut{};
  std::vector<std::uint16_t> stopNt{};
  // The rule of such an exit that ends a region, by cut and edge.
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
  std::uint8_t sectionCfg = 0;

  // A reading whose last boundary is another configuration's, past the region
  // it closes: by the reading it copies and that configuration, and back.
  // Created on demand.
  std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint16_t> openTail{};
  std::unordered_map<std::uint16_t, std::pair<std::uint16_t, std::uint8_t>>
      openOf{};
  std::set<std::uint16_t> openDone{};

  std::vector<LrLowerNode> e{};
  std::vector<LrShapeId> child{};
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
  bool turns(const Acts& made) const {
    return !made.empty() && rowKind[made.front()] == WriteKind::nest;
  }
  // Flags run after their operand in the same construction context. Unlike
  // assign/append, they do not give that operand a separate current node.
  bool only_flags(const Acts& rows) const {
    return std::ranges::all_of(rows, [&](std::uint16_t r) {
      return rowKind[r] == WriteKind::flag;
    });
  }
  // A position whose first reading is a nest turn.
  Descent<bool> head_turns(LrShapeId at) const {
    const LrLowerNode& n = e[at];
    if (turns(n.made)) co_return true;
    if (!n.made.empty() || !only_flags(n.row)) co_return false;
    if (n.kind == LrShape::rep) co_return co_await head_turns(n.sym);
    if (n.kind == LrShape::seq) {
      for (std::size_t i = 0; i < n.count; ++i) {
        const LrShapeId at = child[n.sym + i];
        if (co_await head_turns(at)) co_return true;
        const auto& before = e[at];
        // Conditions and flags do not replace a receiver. A creation or a
        // value write does; a token means this is not the head.
        if (!before.made.empty() || !only_flags(before.row) ||
            (before.kind != LrShape::none && before.kind != LrShape::end &&
             !(before.kind == LrShape::sym &&
               is_gap(static_cast<std::uint16_t>(before.sym)))))
          co_return false;
      }
      co_return false;
    }
    if (n.kind == LrShape::alt || n.kind == LrShape::unordered)
      for (std::size_t i = 0; i < n.count; ++i)
        if (co_await head_turns(child[n.sym + i])) co_return true;
    co_return false;
  }

  LrShapeId push(LrLowerNode x) {
    e.push_back(std::move(x));
    return e.size() - 1;
  }

  Descent<LrShapeId> copy(LrShapeId at) {
    LrLowerNode n = e[at];
    if (n.kind == LrShape::rep || n.kind == LrShape::scope) {
      n.sym = co_await copy(n.sym);
    } else if (n.kind == LrShape::seq || n.kind == LrShape::alt ||
               n.kind == LrShape::unordered) {
      std::vector<LrShapeId> fresh;
      fresh.reserve(n.count);
      for (std::size_t i = 0; i < n.count; ++i)
        fresh.push_back(co_await copy(child[n.sym + i]));
      n.sym = child.size();
      child.insert(child.end(), fresh.begin(), fresh.end());
    }
    const LrShapeId fresh = push(std::move(n));
    if (const auto it = unorderedHead.find(at); it != unorderedHead.end())
      unorderedHead.try_emplace(fresh, it->second);
    if (const auto it = unorderedTail.find(at); it != unorderedTail.end())
      unorderedTail.try_emplace(fresh, it->second);
    co_return fresh;
  }
};

}  // namespace cursiv::engine
