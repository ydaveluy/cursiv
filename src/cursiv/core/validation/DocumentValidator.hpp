#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

// The codes the runtime itself stamps, as opposed to a language's own checks. A
// client comparing `Diagnostic::code` against these acts on the failing
// construct without reading the English back out of the message.

// A reference nothing in scope answers, or one whose resolution cycles.
inline constexpr std::string_view kLinkingError = "linking-error";

// What a linking diagnostic carries besides its sentence: a quick fix
// offering "did you mean X" needs the name that failed, the class that
// declares the member and the member's name -- reading any of them back out
// of the message would make the prose an API.
inline constexpr std::string_view kLinkingRefText = "refText";
inline constexpr std::string_view kLinkingContainerType = "containerType";
inline constexpr std::string_view kLinkingProperty = "property";
// A check that threw; the diagnostic replaces whatever it would have said.
inline constexpr std::string_view kValidationError = "validation-error";
// The parse half: the three codes name what the tree can attest, so a client
// filtering everything syntactic matches all three.
//
// The parser stopped, or recovery gave up: no leaf to point at, only the byte
// it reached.
inline constexpr std::string_view kSyntaxError = "syntax-error";
// Recovery repaired here: the document parses, and the text under this range is
// not what the author wrote.
inline constexpr std::string_view kParseRepaired = "parse-repaired";
// A value its converter refused; the member keeps its default and the AST
// stands.
inline constexpr std::string_view kConversionError = "conversion-error";

// `categories` is a list of identities and empty is every one. The build path
// names what a document still owes; an editor wanting the fast checks alone
// names them.
//
// The two stops apply only when built-in validation runs. A repaired AST is partly
// synthesized, and a check walking it as if the grammar had matched reads
// defaults where the language promised values. Both default off and the build
// path keeps them off -- the incremental registry records a category as run
// when it runs, so a stop that skipped categories while recording them would
// suppress them for ever. They are for direct callers.
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
                                const ValidationOptions&,
                                const CancellationToken&) const = 0;

  // The two spellings above it, so a caller says only what it means.
  void validateDocument(const Document& document,
                        std::vector<Diagnostic>& diagnostics,
                        std::span<const ValidationCategory* const> categories,
                        const CancellationToken& token = {}) const {
    validateDocument(document, diagnostics,
                     ValidationOptions{.categories = categories}, token);
  }
  void validateDocument(const Document& document,
                        std::vector<Diagnostic>& diagnostics,
                        const CancellationToken& token = {}) const {
    validateDocument(document, diagnostics, ValidationOptions{}, token);
  }
};

}  // namespace cursiv
