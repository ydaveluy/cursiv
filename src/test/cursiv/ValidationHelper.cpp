#include "cursiv/ValidationHelper.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv::test {

ValidationResult validationHelper(const CoreServices& services,
                                  std::string text,
                                  const ValidationHelperOptions& options) {
  // `BuildOptions::validation` is already true by default, so the checks run
  // unless the caller said otherwise.
  const std::shared_ptr<Document> document =
      parseHelper(services, std::move(text), options);

  Disposable added = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(added);
  if (options.failOnParsingErrors &&
      (!document->parseResult || document->parseRecovered()))
    expectedFunction("syntax errors", "clean parse", document->uri);
  auto diagnostics = document->diagnostics;
  return {std::move(added), std::move(diagnostics), document, &services};
}

}  // namespace cursiv::test
