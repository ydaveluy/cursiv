#pragma once

#include <memory>
#include <type_traits>

#include <pegium/lsp/hierarchy/CallHierarchyProvider.hpp>
#include <pegium/lsp/code-actions/CodeActionProvider.hpp>
#include <pegium/lsp/code-actions/CodeLensProvider.hpp>
#include <pegium/lsp/completion/CompletionProvider.hpp>
#include <pegium/lsp/navigation/DeclarationProvider.hpp>
#include <pegium/lsp/navigation/DefinitionProvider.hpp>
#include <pegium/lsp/formatting/Formatter.hpp>
#include <pegium/lsp/navigation/DocumentHighlightProvider.hpp>
#include <pegium/lsp/navigation/DocumentLinkProvider.hpp>
#include <pegium/lsp/symbols/DocumentSymbolProvider.hpp>
#include <pegium/lsp/ranges/FoldingRangeProvider.hpp>
#include <pegium/lsp/hover/HoverProvider.hpp>
#include <pegium/lsp/navigation/ImplementationProvider.hpp>
#include <pegium/lsp/semantic/InlayHintProvider.hpp>
#include <pegium/lsp/navigation/ReferencesProvider.hpp>
#include <pegium/lsp/navigation/RenameProvider.hpp>
#include <pegium/lsp/semantic/SemanticTokenProvider.hpp>
#include <pegium/lsp/ranges/SelectionRangeProvider.hpp>
#include <pegium/lsp/semantic/SignatureHelpProvider.hpp>
#include <pegium/lsp/navigation/TypeDefinitionProvider.hpp>
#include <pegium/lsp/hierarchy/TypeHierarchyProvider.hpp>
#include <pegium/lsp/symbols/WorkspaceSymbolProvider.hpp>
#include <pegium/core/services/CoreServices.hpp>

namespace pegium {

struct SharedServices;

/// LSP feature services owned by one language. Each is installed by default and
/// overridable, except those marked `// optional`, whose capability is
/// advertised only when the provider is set.
struct LspFeatureServices {
  std::unique_ptr<CompletionProvider> completionProvider;
  std::unique_ptr<HoverProvider> hoverProvider;
  std::unique_ptr<DocumentSymbolProvider> documentSymbolProvider;
  std::unique_ptr<DocumentHighlightProvider> documentHighlightProvider;
  std::unique_ptr<FoldingRangeProvider> foldingRangeProvider;
  std::unique_ptr<DeclarationProvider> declarationProvider; // optional
  std::unique_ptr<DefinitionProvider> definitionProvider;
  std::unique_ptr<TypeDefinitionProvider> typeProvider; // optional
  std::unique_ptr<ImplementationProvider> implementationProvider; // optional
  std::unique_ptr<ReferencesProvider> referencesProvider;
  std::unique_ptr<RenameProvider> renameProvider;
  std::unique_ptr<CodeActionProvider> codeActionProvider;
  std::unique_ptr<DocumentLinkProvider> documentLinkProvider; // optional
  std::unique_ptr<SelectionRangeProvider> selectionRangeProvider; // optional
  std::unique_ptr<SignatureHelpProvider> signatureHelp; // optional
  std::unique_ptr<CodeLensProvider> codeLensProvider; // optional
  std::unique_ptr<Formatter> formatter; // optional
  std::unique_ptr<InlayHintProvider> inlayHintProvider; // optional
  std::unique_ptr<SemanticTokenProvider> semanticTokenProvider; // optional
  std::unique_ptr<CallHierarchyProvider> callHierarchyProvider; // optional
  std::unique_ptr<TypeHierarchyProvider> typeHierarchyProvider; // optional
};

/// Language service container that extends core services with LSP features.
/// CoreServices is a virtual base so a language can join a core-only and an LSP
/// container under one diamond.
struct Services : virtual pegium::CoreServices {
  explicit Services(const SharedServices &sharedServices);
  // Non-movable like its CoreServices base: installed LSP feature providers hold
  // a back-reference to this Services.
  Services(Services &&) noexcept = delete;
  Services &operator=(Services &&) noexcept = delete;
  Services(const Services &) = delete;
  Services &operator=(const Services &) = delete;
  ~Services() noexcept override;

  const SharedServices &shared;
  LspFeatureServices lsp;
};

/// LSP container bound to a language's own core container `TCore` and LSP shared
/// type `TShared`.
///
/// Combines `TCore` (the language's core services) with the LSP `Services` over
/// one shared `CoreServices`, and re-exposes `shared` typed to the LSP
/// `TShared`. A language derives this and, being the most-derived type, spells
/// out the constructor that initializes the virtual `CoreServices` base:
///
/// ```cpp
/// struct MyServices final
///     : pegium::ServicesFor<MyCoreServices, MySharedServices> {
///   explicit MyServices(const MySharedServices &sharedServices)
///       : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
/// };
/// ```
template <typename TCore, typename TShared> struct ServicesFor : TCore, Services {
  static_assert(std::is_base_of_v<CoreServices, TCore>,
                "TCore must derive from pegium::CoreServices.");
  static_assert(std::is_base_of_v<SharedServices, TShared>,
                "TShared must derive from pegium::SharedServices.");
  static_assert(std::is_constructible_v<TCore, const TShared &>,
                "TCore must be constructible from const TShared&.");

  const TShared &shared;

  explicit ServicesFor(const TShared &sharedServices)
      : CoreServices(sharedServices), TCore(sharedServices),
        Services(sharedServices), shared(sharedServices) {}
};

} // namespace pegium
