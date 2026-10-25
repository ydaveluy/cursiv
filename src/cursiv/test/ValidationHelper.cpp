#include "cursiv/test/ValidationHelper.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "cursiv/test/ParseHelper.hpp"

namespace cursiv::test {

ValidationResult validationHelper(const CoreServices& services,
                                  std::string text,
                                  const ValidationHelperOptions& options) {
  // `BuildOptions::validation` is already true by default, so the checks run
  // unless the caller said otherwise.
  const std::shared_ptr<Document> document =
      parseHelper(services, std::move(text), options);

  // Constructed, never assigned: copy-assigning a `Diagnostic` walks the
  // variant assignment of its `data` payload, which g++ reads as a delete of an
  // object it cannot see allocated.
  ValidationResult result{documentDisposable(services, document),
                          document->diagnostics, document, &services};

  if (options.failOnParsingErrors) {
    // The parse half is three codes in this tree; two of them say the tree is
    // not what the author wrote.
    ExpectDiagnosticOptions syntactic;
    syntactic.severity = DiagnosticSeverity::Error;
    syntactic.code = DiagnosticCode{std::string(kSyntaxError)};
    expectNoIssues(result, syntactic);
    syntactic.code = DiagnosticCode{std::string(kParseRepaired)};
    expectNoIssues(result, syntactic);
  }
  return result;
}

}  // namespace cursiv::test
