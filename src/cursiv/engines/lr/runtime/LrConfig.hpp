// One reading of a repair search: a stack on the cactus, a cursor, the token
// in hand, and where it stands in the fault it is repairing. It writes
// nothing: only the path the search elects is ever written.
#pragma once

#include <cassert>
#include <cstdint>

#include "cursiv/engines/lr/runtime/LrCactusClimb.hpp"
#include "cursiv/engines/lr/runtime/LrPin.hpp"
#include "cursiv/engines/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What a slot of the search stands for. A generator is queued directly and
// never admitted to `LrSearch::seen`: it has no future of its own, only
// gestures of the reading it was made from not yet laid.
enum class LrWorkKind : std::uint8_t {
  // A reading, read forward by the cells' own step.
  read,
  // The abandons of a refused reading, laid one depth at a time.
  enumerateAbandons,
  // The insertions of a refused reading that no owed run reaches.
  enumerateInsertions,
  // The replacement of the word in hand by the keyword its state is forced
  // to, at the price of its deletion and the keyword's insertion.
  forcedReplacement,
};

// Where a point stands in the fault it is repairing, `[abandon] (ins)* (rep |
// del+)?`; the phases are in the order the fault goes through them.
enum class LrRepairPhase : std::uint8_t {
  // No gesture yet at this point.
  beforeGesture,
  // The abandon is laid: insertions, a replacement or deletions go on from it.
  afterAbandon,
  // An insertion is laid, and where it stands more may be.
  inserting,
  // Bytes were dropped or a trivia skipped: only more of them.
  discardOnly,
};

struct LrConfig : LrPin {
  LrCactusClimb run;
  TextOffset at = 0;
  Token la{};
  std::uint16_t laMode = kNoMode;
  bool done = false;
  bool dead = false;
  LrVirtualAt virt;
  IndentAt indent;
  // Shifted by a state that owed it.
  bool pinDue = false;

  // The key; the rank is read through `link`.
  std::uint32_t cost = 0;
  std::uint32_t lost = 0;
  std::uint32_t gestures = 0;
  std::uint32_t faults = 0;
  std::uint32_t invented = 0;
  std::uint32_t link = kNoGestureLink;
  // Owed insertions still free at this point. A paid insertion opens a new
  // allowance bounded by the stack it set out on; real text ends it.
  std::uint64_t freeInserts = 0;
  // Real tokens since the last gesture, up to `kRepairValidateShifts`.
  std::uint8_t shifts = 0;
  LrRepairPhase phase = LrRepairPhase::beforeGesture;
  // No gesture yet at this point: the next one opens a fault. A generator has
  // paid the fault it opens when it was made, and is never counted again.
  bool fresh = true;
  // A generator stands for gestures of `source()` not yet made.
  LrWorkKind work = LrWorkKind::read;
  // Dominated, or an arm its own action refused: never read again.
  bool gone = false;
  // Its bound read from the text ahead: queued again at it once.
  bool weighed = false;
  // Named by a queue entry: its slot outlives the gesture that laid it.
  bool queued = false;
  // The arm the next fork of this step hands out; zero, none yet: the reading
  // itself becomes arm 0 at its first fork. A generator of abandons keeps its
  // next choice here.
  std::uint32_t armNext = 0;

  bool generates() const { return work != LrWorkKind::read; }

  // Which gestures may come next at the point: an abandon before any gesture;
  // a replacement until bytes are dropped; an insertion until then too, but in
  // front of bytes no mode reads (`garbage`), one at most.
  bool mayAbandon() const { return phase == LrRepairPhase::beforeGesture; }
  bool mayReplace() const { return phase <= LrRepairPhase::inserting; }
  bool mayInsert(bool garbage) const {
    return phase < LrRepairPhase::inserting ||
           (phase == LrRepairPhase::inserting && !garbage);
  }

  // The generator for the insertions of the reading `base`, which stands at
  // `source`; `rank` is the rank base of the non-owed terminals left to try.
  static LrConfig insertGenerator(const LrConfig& base, std::uint32_t source,
                                  std::uint32_t rank) {
    LrConfig g = base;
    g.work = LrWorkKind::enumerateInsertions;
    g._source = source;
    g._keep = rank;
    return g;
  }

  // The generator for the replacement of the word in hand of the reading
  // `base`, which stands at `source`; `rank` is the rank it is laid at.
  static LrConfig replaceGenerator(const LrConfig& base, std::uint32_t source,
                                   std::uint32_t rank) {
    LrConfig g = insertGenerator(base, source, rank);
    g.work = LrWorkKind::forcedReplacement;
    return g;
  }

  // The generator for the abandons of the reading `base`, which stands at
  // `source`: from `depth` frames down, whose top is cactus node `node` with
  // begin node `begin`; `arm` is the choice of the first.
  static LrConfig abandonGenerator(const LrConfig& base, std::uint32_t source,
                                   std::uint32_t depth, std::uint32_t node,
                                   std::uint32_t begin, std::uint32_t arm) {
    LrConfig g = base;
    g.work = LrWorkKind::enumerateAbandons;
    g._source = source;
    g.resumeAbandons(depth, node, begin, arm);
    return g;
  }

  // The slot of the reading a generator was made from.
  std::uint32_t source() const {
    assert(generates());
    return _source;
  }
  // Of insertions: the rank base of the non-owed terminals left to try; of a
  // replacement, its rank.
  std::uint32_t insertRank() const {
    assert(work == LrWorkKind::enumerateInsertions ||
           work == LrWorkKind::forcedReplacement);
    return _keep;
  }
  // Of abandons: the frames the next one gives up, the cactus node at the top
  // of them and its begin node.
  std::uint32_t abandonDepth() const {
    assert(work == LrWorkKind::enumerateAbandons);
    return _keep;
  }
  std::uint32_t abandonNode() const {
    assert(work == LrWorkKind::enumerateAbandons);
    return _node;
  }
  std::uint32_t abandonBegin() const {
    assert(work == LrWorkKind::enumerateAbandons);
    return _begin;
  }
  // The generator goes on from another depth, its next choice `arm`.
  void resumeAbandons(std::uint32_t depth, std::uint32_t node,
                      std::uint32_t begin, std::uint32_t arm) {
    assert(work == LrWorkKind::enumerateAbandons);
    _keep = depth;
    _node = node;
    _begin = begin;
    armNext = arm;
  }

  // The author's bytes read again are a token of theirs, but no shift past
  // the gesture that read them.
  template <const auto& A>
  void shifted(bool laid) {
    if (pin && la == pinTok) {
      pin = false;
      if (!pinRep) {
        pinDue = A.cold
                     .close_row(static_cast<std::uint16_t>(
                         run.state_at(run.size() - 2)))
                     .test(pinTok.sym);
        la = held;
        laMode = heldMode;
        return;
      }
    } else if (laid) {
      return;
    } else if (shifts < kRepairValidateShifts) {
      ++shifts;
    }
    run.add_token();
    fresh = true;
    phase = LrRepairPhase::beforeGesture;
    freeInserts = 0;
  }

 private:
  // A generator's payload; `_keep` is a rank base for insertions, a rank for
  // a replacement and a depth for abandons, read through the accessors of its
  // own kind.
  std::uint32_t _source = 0;
  std::uint32_t _keep = 0;
  std::uint32_t _node = 0;
  std::uint32_t _begin = 0;
};

}  // namespace cursiv
