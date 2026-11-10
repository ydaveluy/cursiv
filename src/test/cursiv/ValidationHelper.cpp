#include "cursiv/ValidationHelper.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ParseHelper.hpp"

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
  ValidationResult result{detail::documentDisposable(services, document),
                          document->diagnostics, document, &services};
  const detail::DisposeOnThrow cleanup(result);

  if (options.failOnParsingErrors) {
    // Of the parse codes, these two say the tree is not what the author wrote.
    ExpectDiagnosticOptions syntactic;
    syntactic.severity = DiagnosticSeverity::Error;
    for (const std::string_view code : {kSyntaxError, kParseRepaired}) {
      JsonValue::Object wanted;
      wanted.try_emplace(std::string(kDataCode), std::string(code));
      syntactic.data.emplace(std::move(wanted));
      expectNoIssues(result, syntactic);
    }
  }
  return result;
}

}  // namespace cursiv::test
