#include "cursiv/lsp/workspace/LspDocumentBuilder.hpp"

#include <optional>
#include <span>
#include <string>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/workspace/ConfigurationProvider.hpp"
#include "lsp/types.h"

namespace cursiv {

LspDocumentBuilder::LspDocumentBuilder(SharedLspServices& shared)
    : DefaultDocumentBuilder(shared), _lsp(shared) {}

// `update` hands its changed documents the update's request, by reference:
// each is asked its own language's instead.
void LspDocumentBuilder::prepareBuild(std::span<Document* const> documents,
                                      const BuildOptions& options) const {
  if (&options != &updateBuildOptions()) {
    DefaultDocumentBuilder::prepareBuild(documents, options);
    return;
  }
  for (Document* const document : documents)
    DefaultDocumentBuilder::prepareBuild({&document, 1},
                                         configuredBuildOptions(*document));
}

// A document relinked by an update carries no request of its own -- its reset
// took it -- or a finished one: either way it is asked its own language's. An
// unfinished request, explicit ones included, survives.
void LspDocumentBuilder::buildDocuments(std::span<Document* const> documents,
                                        const CancellationToken& token) const {
  for (Document* const document : documents)
    DefaultDocumentBuilder::prepareBuild({&document, 1},
                                         configuredBuildOptions(*document));
  DefaultDocumentBuilder::buildDocuments(documents, token);
}

// The section is the language id `DefaultConfigurationProvider::sections()`
// requests under.
BuildOptions LspDocumentBuilder::configuredBuildOptions(
    const Document& document) const {
  const CoreServices* const services = document.language;
  if (services == nullptr) return updateBuildOptions();
  const std::string& languageId = services->languageMetaData.languageId;
  if (languageId.empty()) return updateBuildOptions();
  const std::optional<lsp::LSPAny> validation =
      _lsp.configurationProvider->getConfiguration(languageId, "validation");
  if (!validation.has_value()) return updateBuildOptions();
  return BuildOptions{
      true, validation_categories(*validation, *services->validationRegistry)};
}

}  // namespace cursiv
