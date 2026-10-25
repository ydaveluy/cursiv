#pragma once

#include <span>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/workspace/Document.hpp"

// The "did you mean" fixes of a linking error: the names its reference's scope
// holds within a bounded edit distance, as default quick-fix rows. Computed
// when a client asks for one diagnostic's fixes, never by the validation --
// one lookup walks the whole scope, the global index included.

namespace cursiv {

// The rows of each diagnostic, in its order: one replacement row each, the
// nearest first, three at most; none for a diagnostic that is not a not-found
// reference, or whose reference is past the first sites of its document -- a
// missing import breaks every reference of a document, and none of them is a
// misspelling. A request asks for all its diagnostics at once: the document's
// references are walked once for them, never once each.
[[nodiscard]] std::vector<JsonValue::Array> spelling_actions(
    const Document& document, std::span<const Diagnostic* const> diagnostics);

}  // namespace cursiv
