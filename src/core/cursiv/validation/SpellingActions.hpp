#pragma once

#include <span>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"

// The "did you mean" fixes of a linking error: names in the reference's scope
// within a bounded edit distance. Computed on request, never by validation.

namespace cursiv::detail {

// The rows of each diagnostic, in order: up to three replacements, nearest
// first. None for a diagnostic that is not a not-found reference, or past the
// first sites of the request: `spelt` counts them over all its calls.
[[nodiscard]] std::vector<JsonValue::Array> spelling_actions(
    const Document& document, std::span<const Diagnostic* const> diagnostics,
    unsigned& spelt, const CancellationToken& token = {});

}  // namespace cursiv::detail
