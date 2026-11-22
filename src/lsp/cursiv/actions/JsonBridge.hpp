#pragma once

#include <lsp/types.h>

#include "cursiv/utils/JsonValue.hpp"

namespace cursiv {
struct Diagnostic;
struct Document;
class Documents;
}  // namespace cursiv

namespace cursiv::detail {

[[nodiscard]] lsp::LSPAny to_lsp_any(const JsonValue& value);

[[nodiscard]] lsp::Diagnostic to_lsp_diagnostic(const Document& document,
                                                const Diagnostic& diagnostic,
                                                const Documents& documents);

}  // namespace cursiv::detail
