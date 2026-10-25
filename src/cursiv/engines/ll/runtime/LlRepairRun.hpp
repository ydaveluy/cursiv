// The gestures a repair makes with no token read between them: one fault, and
// one note once the run ends.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/RepairFault.hpp"

namespace cursiv {

struct LlRepairRun : RepairFault {
  // A misspelt word's span: whatever reads it lays its leaf.
  TextOffset spelt = 0, speltEnd = 0;
  // The cursor after the last gesture: one made at the same cursor joins.
  TextOffset at = 0;
  // The alphabet the last drop was read with: the token it resumes on is.
  std::uint16_t dropMode = 0;
};

}  // namespace cursiv
