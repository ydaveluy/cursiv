// What each entry of an LR record table answers, and the completion site it
// stands for: entries naming one member of one owner, narrowed alike, share a
// site.
#pragma once

#include <span>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/shared/gen/AstEntryTable.hpp"
#include "cursiv/engines/shared/gen/AstRowGen.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"

namespace cursiv {

std::vector<RecordGen> lr_record_gens(const AstEntryTable& table,
                                      std::span<const AstRowGen> rows);

std::vector<ElementId> lr_completion_sites(
    const std::vector<RecordGen>& records);

}  // namespace cursiv
