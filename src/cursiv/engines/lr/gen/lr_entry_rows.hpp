// The record table as the AST emitter reads it: an entry is a chain's own, or
// a fused cell's node followed by its site's chain. A turn says by its row
// that it has a receiver.
#pragma once

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/shared/gen/AstEntryTable.hpp"

namespace cursiv {

AstEntryTable lr_entry_table(const LrBuildTable& a);

}  // namespace cursiv
