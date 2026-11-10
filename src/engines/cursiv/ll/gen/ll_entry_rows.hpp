// An entry of the record table as the rows it runs: a chain's own, or the
// halves of a record the construction fused.
#pragma once

#include <span>
#include <vector>

#include "cursiv/shared/gen/AstEntryTable.hpp"
#include "cursiv/shared/gen/AstRowGen.hpp"
#include "cursiv/shared/gen/EmitRecords.hpp"

namespace cursiv::engine {

class LlFinalProgram;

// The AST emitter's reading: the halves, or one run where the site makes a
// node. A join writes the closes of every rule a call may return through; a
// creation over a made node is its own scope.
AstEntryTable ll_entry_table(const LlFinalProgram& done,
                             std::span<const AstRowGen> rows,
                             std::vector<RecordGen>& records);

}  // namespace cursiv::engine
