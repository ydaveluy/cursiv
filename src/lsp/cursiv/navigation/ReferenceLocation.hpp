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

// Converts reference rows to `Location`s, in order; a row of a document the
// store does not hold is left out.
[[nodiscard]] std::vector<lsp::Location> reference_locations(
    const Documents& store, std::span<const ReferenceDescription> references);

}  // namespace detail

}  // namespace cursiv
