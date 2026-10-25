// One reading of a repair search: a stack on the cactus, a cursor, the token
// in hand, and where it stands in the fault it is repairing. It writes
// nothing: only the path the search elects is ever written.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/runtime/LrCactusClimb.hpp"
#include "cursiv/engines/lr/runtime/LrPin.hpp"
#include "cursiv/engines/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

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
  std::uint32_t faults = 0;
  std::uint32_t invented = 0;
  std::uint32_t link = kNoGestureLink;
  // Owed insertions still free at this point. A paid insertion opens a new
  // allowance bounded by the stack it set out on; real text ends it.
  std::uint64_t freeInserts = 0;
  // Real tokens since the last gesture, up to `kRepairValidateShifts`.
  std::uint8_t shifts = 0;
  // Where the point stands in `[abandon] (ins)* (rep | del+)?`; in front of
  // bytes no mode reads, one insertion at most.
  std::uint8_t phase = 0;
  // No gesture yet at this point: the next one opens a fault.
  bool fresh = true;
  // Stands for gestures of `genFrom` not yet made: the abandons from frame
  // `genKeep` down, laid one depth at a time; for `genInserts`, `genKeep`
  // is the rank base of the non-owed terminals left to try.
  bool gen = false;
  bool genInserts = false;
  std::uint32_t genFrom = 0;
  std::uint32_t genKeep = 0;
  std::uint32_t genNode = 0;
  std::uint32_t genBegin = 0;
  // Dominated, or an arm its own action refused: never read again.
  bool gone = false;
  // Its bound read from the text ahead: queued again at it once.
  bool weighed = false;
  // Named by a queue entry: its slot outlives the gesture that laid it.
  bool queued = false;
  // The arm the next fork of this step hands out; zero, none yet: the reading
  // itself becomes arm 0 at its first fork. A generator's next choice.
  std::uint32_t armNext = 0;

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
    phase = 0;
    freeInserts = 0;
  }
};

}  // namespace cursiv
