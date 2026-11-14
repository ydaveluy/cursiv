#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// The codes the runtime itself writes in a diagnostic's `data`, as opposed to a
// language's own checks. A code action comparing `data_code` against these acts
// on the failing construct without reading the English back out of the message.

// A reference nothing in scope answers, or one whose resolution cycles.
inline constexpr std::string_view kLinkingError = "linking-error";

// What a linking diagnostic carries besides its sentence, for a "did you mean"
// fix: the failed name, the declaring class and the member's name.
inline constexpr std::string_view kLinkingRefText = "refText";
inline constexpr std::string_view kLinkingContainerType = "containerType";
inline constexpr std::string_view kLinkingProperty = "property";
// A check that threw; the diagnostic replaces whatever it would have said.
inline constexpr std::string_view kValidationError = "validation-error";
// The parse half: three codes, so a client can filter all syntax errors. The
// parser stopped, or recovery gave up: only the byte it reached is known.
inline constexpr std::string_view kSyntaxError = "syntax-error";
// Recovery repaired here: the document parses, and the text under this range is
// not what the author wrote.
inline constexpr std::string_view kParseRepaired = "parse-repaired";
// A value its converter refused; the member keeps its default and the AST
// stands.
inline constexpr std::string_view kConversionError = "conversion-error";

// `categories` lists identities; empty is every one. The two stops apply to
// built-in validation only, and are for direct callers: the build keeps them
// off, as it records the categories it runs.
struct ValidationOptions {
  std::span<const ValidationCategory* const> categories{};
  bool stopAfterParsingErrors = false;
  bool stopAfterLinkingErrors = false;
};

// Validation adds to the diagnostics it receives: what was in the vector stays.
class DocumentValidator {
 public:
  virtual ~DocumentValidator() noexcept = default;
  virtual void validateDocument(const Document&, std::vector<Diagnostic>&,
                                const ValidationOptions& = {},
                                const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
