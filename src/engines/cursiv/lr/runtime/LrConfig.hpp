// One reading of a repair search: a stack on the cactus, a cursor, the token
// in hand, and where it stands in the fault it is repairing. It writes
// nothing: only the path the search elects is ever written.
#pragma once

#include <cassert>
#include <cstdint>

#include "cursiv/lr/runtime/LrCactusView.hpp"
#include "cursiv/lr/runtime/LrPin.hpp"
#include "cursiv/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

struct LrConfig : LrPin, RepairReading {
  LrCactusView run;
  TextOffset at = 0;
  Token la{};
  std::uint16_t laMode = kNoMode;
  bool dead = false;
  LrVirtualAt virt;
  IndentAt indent;
  // Shifted by a state that needed it.
  bool pinDue = false;
  // Required insertions still free at this point. A costed insertion opens a
  // new allowance bounded by the stack it set out on; real text ends it.
  std::uint64_t freeInserts = 0;
  // The arm the next fork of this step hands out; zero, none yet: the reading
  // itself becomes arm 0 at its first fork. A generator of abandons keeps its
  // next choice here.
  std::uint32_t armNext = 0;

  // The generator for the insertions of the reading `base`, which stands at
  // `source`; `rank` is the rank base of the other terminals left to try.
  static LrConfig insertGenerator(const LrConfig& base, std::uint32_t source,
                                  std::uint32_t rank) {
    LrConfig g = base;
    g.work = RepairWork::enumerateInsertions;
    g._source = source;
    g._keep = rank;
    return g;
  }

  // The generator for the replacement of the word in hand of the reading
  // `base`, which stands at `source`; `rank` is the rank it is made at.
  static LrConfig replaceGenerator(const LrConfig& base, std::uint32_t source,
                                   std::uint32_t rank) {
    LrConfig g = insertGenerator(base, source, rank);
    g.work = RepairWork::forcedReplacement;
    return g;
  }

  // The generator for the abandons of the reading `base`, which stands at
  // `source`: from `depth` frames down, whose top is cactus node `node` with
  // begin node `begin`; `arm` is the choice of the first.
  static LrConfig abandonGenerator(const LrConfig& base, std::uint32_t source,
                                   std::uint32_t depth, std::uint32_t node,
                                   std::uint32_t begin, std::uint32_t arm) {
    LrConfig g = base;
    g.work = RepairWork::enumerateAbandons;
    g._source = source;
    g.resumeAbandons(depth, node, begin, arm);
    return g;
  }

  // The slot of the reading a generator was made from.
  std::uint32_t source() const {
    assert(generates());
    return _source;
  }
  // Of insertions: the rank base of the other terminals left to try; of a
  // replacement, its rank.
  std::uint32_t insertRank() const {
    assert(work == RepairWork::enumerateInsertions ||
           work == RepairWork::forcedReplacement);
    return _keep;
  }
  // Of abandons: the frames the next one keeps, the cactus node at the top of
  // those it gives up and its begin node.
  std::uint32_t abandonDepth() const {
    assert(work == RepairWork::enumerateAbandons);
    return _keep;
  }
  std::uint32_t abandonNode() const {
    assert(work == RepairWork::enumerateAbandons);
    return _node;
  }
  std::uint32_t abandonBegin() const {
    assert(work == RepairWork::enumerateAbandons);
    return _begin;
  }
  // The generator goes on from another depth, its next choice `arm`.
  void resumeAbandons(std::uint32_t depth, std::uint32_t node,
                      std::uint32_t begin, std::uint32_t arm) {
    assert(work == RepairWork::enumerateAbandons);
    _keep = depth;
    _node = node;
    _begin = begin;
    armNext = arm;
  }

  // The author's bytes read again are a token of theirs, but no shift past
  // the edit that read them.
  template <const auto& A>
  void shifted(bool synthetic) {
    if (pin && la == pinTok) {
      pin = false;
      if (!pinRep) {
        const auto under =
            static_cast<std::uint16_t>(run.state_at(run.size() - 2));
        pinDue = A.cold.close[under].test(pinTok.sym);
        la = held;
        laMode = heldMode;
        return;
      }
    } else if (synthetic) {
      return;
    } else if (shifts < kRepairValidateShifts) {
      ++shifts;
    }
    run.add_token();
    fresh = true;
    phase = RepairPhase::beforeEdit;
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

}  // namespace cursiv::engine
