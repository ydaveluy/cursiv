// The record table as the AST emitter reads it, what each of its entries
// answers, and the completion site it stands for.
#pragma once

#include <span>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/gen/LrBuildTable.hpp"
#include "cursiv/shared/gen/AstEntryTable.hpp"
#include "cursiv/shared/gen/AstRowGen.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/EmitRecords.hpp"

namespace cursiv::engine {

// An entry is a chain's own, or a fused cell's node followed by its site's
// chain. A turn says by its row that it has a receiver.
AstEntryTable lr_entry_table(const LrBuildTable& a);

std::vector<RecordGen> lr_record_gens(const AstEntryTable& table,
                                      std::span<const AstRowGen> rows,
                                      const ChainPool& writes);

// Entries naming one member of one owner, narrowed the same, share a site.
std::vector<ElementId> lr_completion_sites(
    const std::vector<RecordGen>& records);

}  // namespace cursiv::engine
