#include "cursiv/workspace/LspDocumentBuilder.hpp"

#include <optional>
#include <span>
#include <string>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"
#include "lsp/types.h"

namespace cursiv {

LspDocumentBuilder::LspDocumentBuilder(SharedLspServices& shared)
    : DefaultDocumentBuilder(shared), _lsp(shared) {}

// The section is the language id `DefaultConfigurationProvider::sections()`
// requests under.
BuildOptions LspDocumentBuilder::updateBuildOptionsFor(
    const CoreServices& language) const {
  const std::string& languageId = language.languageMetaData.languageId;
  if (languageId.empty()) return updateBuildOptions();
  const std::optional<lsp::LSPAny> validation =
      _lsp.configurationProvider->getConfiguration(languageId, "validation");
  if (!validation.has_value()) return updateBuildOptions();
  return BuildOptions{true, detail::validation_categories(
                                *validation, *language.validationRegistry)};
}

}  // namespace cursiv
