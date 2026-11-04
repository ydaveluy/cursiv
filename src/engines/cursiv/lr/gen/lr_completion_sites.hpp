// What each entry of an LR record table answers, and the completion site it
// stands for: entries naming one member of one owner, narrowed alike, share a
// site.
#pragma once

#include <span>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/shared/gen/AstEntryTable.hpp"
#include "cursiv/shared/gen/AstRowGen.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/EmitRecords.hpp"

namespace cursiv {

std::vector<RecordGen> lr_record_gens(const AstEntryTable& table,
                                      std::span<const AstRowGen> rows,
                                      const ChainPool& writes);

std::vector<ElementId> lr_completion_sites(
    const std::vector<RecordGen>& records);

}  // namespace cursiv
