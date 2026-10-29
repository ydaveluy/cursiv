// What one faulted document scored, as counts: a class sums them before it
// divides, so a document weighs what it holds.
#pragma once

#include <cstdint>
#include <vector>

#include "recovery_metric/support/AstOverlap.hpp"
#include "recovery_metric/support/TextKept.hpp"

namespace cursiv::recovery_metric {

struct RepairVerdict {
  std::uint32_t faults = 0, diagnostics = 0;
  std::uint32_t found = 0, missed = 0, parasites = 0;
  bool accepted = false;
  // Accepted with no syntax diagnostic: every fault it holds is a miss.
  bool silent = false;
  // Silent with the twin's own tree: the faults never reached the language,
  // and the document is counted apart from every rate.
  bool noop = false;
  bool tree = false;
  TextKept text;
  AstOverlap::Result ast;
  // parse(faulted) over parse(valid), in retired instructions; negative when
  // no counter was read.
  double cost = -1;
  // How often the parse reached each ceiling its caller names.
  std::vector<std::uint32_t> ceilings;
};

}  // namespace cursiv::recovery_metric
