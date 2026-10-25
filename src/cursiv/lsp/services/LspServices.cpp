// Every slot's type must be complete here, even the ones this file never
// names: a `ServiceSlot` holds a `unique_ptr`, whose destructor needs the
// whole type. An include-cleaner sweep would flag these as unused;
// removing one fails the build on an incomplete type.
#include "cursiv/lsp/services/LspServices.hpp"  // NOLINT(misc-include-cleaner)

#include <memory>

#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/lsp/actions/CodeLensProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/actions/DocumentLinkProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/completion/DefaultCompletionProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/formatting/Formatter.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/hierarchy/CallHierarchyProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/hierarchy/TypeHierarchyProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/hover/DefaultHoverProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/DeclarationProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/DefaultDefinitionProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/DefaultDocumentHighlightProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/DefaultReferencesProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/DefaultRenameProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/ImplementationProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/navigation/TypeDefinitionProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/ranges/DefaultFoldingRangeProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/ranges/DefaultSelectionRangeProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/semantic/DefaultSemanticTokenProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/semantic/InlayHintProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/semantic/SignatureHelpProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/symbols/DefaultDocumentSymbolProvider.hpp"  // NOLINT(misc-include-cleaner)

namespace cursiv {

LspServices::LspServices(SharedLspServices& sharedServices) noexcept
    : CoreServices(sharedServices), lspShared(sharedServices) {}

LspServices::~LspServices() noexcept = default;

// The core rows first, then this storey's: `firstMissingService` reports the
// first thing owed, and a language owes its parser before it owes an outline.
void LspServices::visitServices(
    cursiv::function_ref<void(const ServiceSlot&)> yield) const {
  CoreServices::visitServices(yield);
  visitOwnServices(yield);
}

// This storey's rows alone, so a language container combining several
// storeys can emit each set once whatever its diamond looks like. None is
// required: a language answers the requests it installs a provider for.
void LspServices::visitOwnServices(
    cursiv::function_ref<void(const ServiceSlot&)> yield) const {
  yield({"documentSymbolProvider", false, documentSymbolProvider != nullptr});
  yield({"declarationProvider", false, declarationProvider != nullptr});
  yield({"implementationProvider", false, implementationProvider != nullptr});
  yield({"typeDefinitionProvider", false, typeDefinitionProvider != nullptr});
  yield({"callHierarchyProvider", false, callHierarchyProvider != nullptr});
  yield({"typeHierarchyProvider", false, typeHierarchyProvider != nullptr});
  yield({"signatureHelpProvider", false, signatureHelpProvider != nullptr});
  yield({"inlayHintProvider", false, inlayHintProvider != nullptr});
  yield({"documentLinkProvider", false, documentLinkProvider != nullptr});
  yield({"codeLensProvider", false, codeLensProvider != nullptr});
  yield({"definitionProvider", false, definitionProvider != nullptr});
  yield({"referencesProvider", false, referencesProvider != nullptr});
  yield({"renameProvider", false, renameProvider != nullptr});
  yield({"documentHighlightProvider", false,
         documentHighlightProvider != nullptr});
  yield({"semanticTokenProvider", false, semanticTokenProvider != nullptr});
  yield({"foldingRangeProvider", false, foldingRangeProvider != nullptr});
  yield({"selectionRangeProvider", false, selectionRangeProvider != nullptr});
  yield({"hoverProvider", false, hoverProvider != nullptr});
  yield({"completionProvider", false, completionProvider != nullptr});
  yield({"formatter", false, formatter != nullptr});
  yield({"codeActionProvider", false, codeActionProvider != nullptr});
}

void install_default_lsp_services(LspServices& services) {
  install_default_core_services(services);
  if (services.documentSymbolProvider == nullptr)
    services.documentSymbolProvider =
        std::make_unique<DefaultDocumentSymbolProvider>(services);
  if (services.definitionProvider == nullptr)
    services.definitionProvider =
        std::make_unique<DefaultDefinitionProvider>(services);
  if (services.referencesProvider == nullptr)
    services.referencesProvider =
        std::make_unique<DefaultReferencesProvider>(services);
  if (services.renameProvider == nullptr)
    services.renameProvider = std::make_unique<DefaultRenameProvider>(services);
  if (services.documentHighlightProvider == nullptr)
    services.documentHighlightProvider =
        std::make_unique<DefaultDocumentHighlightProvider>(services);
  // A default the reference leaves to the language: the colouring reads only
  // the grammar model and the name provider, so every language has one.
  if (services.semanticTokenProvider == nullptr)
    services.semanticTokenProvider =
        std::make_unique<DefaultSemanticTokenProvider>(services);
  if (services.foldingRangeProvider == nullptr)
    services.foldingRangeProvider =
        std::make_unique<DefaultFoldingRangeProvider>(services);
  // A selection chain is read off the CST alone, so its provider holds no
  // service.
  if (services.selectionRangeProvider == nullptr)
    services.selectionRangeProvider =
        std::make_unique<DefaultSelectionRangeProvider>();
  if (services.hoverProvider == nullptr)
    services.hoverProvider = std::make_unique<DefaultHoverProvider>(services);
  if (services.completionProvider == nullptr)
    services.completionProvider =
        std::make_unique<DefaultCompletionProvider>(services);
  // The quick fixes: the validator stamped the possible repairs on the
  // diagnostics; this turns them into client edits.
  if (services.codeActionProvider == nullptr)
    services.codeActionProvider = std::make_unique<DefaultCodeActionProvider>();
}

}  // namespace cursiv
