#include <pegium/lsp/navigation/DefaultReferencesProvider.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

#include <utility>

#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>

namespace pegium {
using namespace pegium::provider_detail;

std::vector<::lsp::Location> DefaultReferencesProvider::findReferences(
    const workspace::Document &document, const ::lsp::ReferenceParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto offset = document.textDocument().offsetAt(params.position);
  const auto includeDeclaration = params.context.includeDeclaration;
  const auto &referencesService = *services.references.references;

  std::vector<::lsp::Location> locations;
  utils::TransparentStringSet seen;

  for (const auto *target :
       find_declarations_at_offset(document, offset, referencesService)) {
    for (const auto &reference : referencesService.findReferences(
             *target, {.includeDeclaration = includeDeclaration})) {
      utils::throw_if_cancelled(cancelToken);
      const auto location = to_location(reference);
      if (!seen.insert(location_key(location)).second) {
        continue;
      }
      // An indexed reference (from a value-copy index snapshot) can outlive its
      // document in the store, e.g. when this read API is used outside the
      // workspace lock. Skip such stale references instead of dereferencing
      // null; the held shared_ptr keeps the document alive across positionAt.
      const auto targetDocument =
          services.shared.workspace.documents->getDocument(location.documentId);
      if (targetDocument == nullptr) {
        continue;
      }
      const auto &textDocument = targetDocument->textDocument();

      ::lsp::Location result{};
      result.uri = ::lsp::Uri::parse(targetDocument->uri);
      result.range = offset_to_range(textDocument, location.begin, location.end);
      locations.push_back(std::move(result));
    }
  }
  return locations;
}

} // namespace pegium
