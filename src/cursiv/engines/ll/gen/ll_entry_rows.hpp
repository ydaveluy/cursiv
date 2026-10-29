// An entry of the record table as the rows it runs: a chain's own, or the
// halves of a record the construction fused.
#pragma once

#include <span>
#include <vector>

#include "cursiv/engines/shared/gen/AstEntryTable.hpp"
#include "cursiv/engines/shared/gen/AstRowGen.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"

namespace cursiv {

class LlFinalProgram;

// The AST emitter's reading: the halves, or one run where the site makes a
// node. A join lays the closes of every rule a call may return through on the
// record, and the record's first maker is its node: a close typed for a turn
// the climb did not take adopts nothing. A creation over a node already made
// is a scope of its own: the runs follow one another, as the table's do.
AstEntryTable ll_entry_table(const LlFinalProgram& done,
                             std::span<const AstRowGen> rows,
                             std::vector<RecordGen>& records);

}  // namespace cursiv
