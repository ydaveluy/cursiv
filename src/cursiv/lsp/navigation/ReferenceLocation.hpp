// One row of the inverse index, as the protocol spells it: a
// `ReferenceDescription` names its document by id and its extent in
// bytes; a client reads a URI and a UTF-16 range instead.
#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/core/workspace/ReferenceDescription.hpp"

namespace cursiv {

class Documents;

/**
 * Converts one reference row to a `Location`. Shared by find-references,
 * rename, and highlight, so the "document the store no longer holds"
 * rule -- silently not listed, never an error -- is written once.
 */
[[nodiscard]] std::optional<lsp::Location> reference_location(
    const Documents& store, const ReferenceDescription& reference);

}  // namespace cursiv
