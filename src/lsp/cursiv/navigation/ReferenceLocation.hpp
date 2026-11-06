// Rows of the inverse index, as the protocol spells them: a
// `ReferenceDescription` names its document by id and its extent in
// bytes; a client reads a URI and a UTF-16 range instead.
#pragma once

#include <lsp/types.h>

#include <span>
#include <vector>

#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv {

class Documents;

namespace detail {

/**
 * Converts reference rows to `Location`s, in their order. Shared by
 * find-references and rename, so the "document the store no longer holds"
 * rule -- silently not listed, never an error -- is written once. A run of
 * rows from one document looks it up and reads its URI once.
 */
[[nodiscard]] std::vector<lsp::Location> reference_locations(
    const Documents& store, std::span<const ReferenceDescription> references);

}  // namespace detail

}  // namespace cursiv
