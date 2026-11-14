#pragma once

#include <lsp/types.h>

#include "cursiv/utils/JsonValue.hpp"

// Converts between the core's JSON (`cursiv::JsonValue`, a diagnostic's
// `data`) and the framework's wire value.

namespace cursiv::detail {

[[nodiscard]] lsp::LSPAny to_lsp_any(const JsonValue& value);

}  // namespace cursiv::detail
