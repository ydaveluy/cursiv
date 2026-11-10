// Exercises one code-action fix end to end: the input carries exactly one
// finding with the code, and the offered action must give the output and
// clear it. The result's `document` is the input, kept until `dispose()`.
#pragma once

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/ParseHelperOptions.hpp"

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
