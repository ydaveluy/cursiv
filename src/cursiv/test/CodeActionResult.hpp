// Exercises one code-action fix end to end: the input must carry exactly
// one finding with the given code, and the single offered action (if any)
// must produce the given output and clear the finding.
#pragma once

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct Document;
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct CodeActionResult : Disposable {
  std::shared_ptr<Document> document;
  std::vector<Diagnostic> diagnosticsAll;
  Diagnostic diagnosticRelevant;
  // Absent when no action was expected.
  std::optional<lsp::CodeAction> action;
};

[[nodiscard]] CodeActionResult testCodeAction(
    const LspServices& services, std::string input,
    std::string_view diagnosticCode,
    std::optional<std::string_view> outputAfterFix,
    const ParseHelperOptions& options = {});

}  // namespace cursiv::test
