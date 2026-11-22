// One text read by a language's tables into a CST.
#pragma once

#include <string_view>

#include "cursiv/engine/parser/Tables.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv::engine {

// Fills `cst` with the records of `text` and answers whether the whole text
// was read as the entry rule. A stop leaves `cst` empty and throws
// `OperationCancelled`.
bool parse(const Tables& tables, std::string_view text, Cst& cst,
           const CancellationToken& cancelToken = kNoCancel);

// Reserves in `tape` the room the records of a text of `bytes` take.
void reserve_for(CstRun& tape, std::size_t bytes);

}  // namespace cursiv::engine
