#pragma once

#include <lsp/types.h>

#include "cursiv/core/services/JsonValue.hpp"

// The one crossing between the core's owned JSON (`cursiv::JsonValue`, what a
// diagnostic's `data` carries) and the framework's wire value. Written once,
// at the junction: the core never learns the framework's type, the framework
// never learns the core's.

namespace cursiv {

[[nodiscard]] lsp::LSPAny to_lsp_any(const JsonValue& value);

}  // namespace cursiv
