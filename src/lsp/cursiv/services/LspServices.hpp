// The core container is the base: a language that is both core and LSP is one
// object, and `visitServices` lists both sets of services.
#pragma once

#include <memory>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceSlot.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/services/SharedLspServices.hpp"

namespace cursiv {

class CodeActionProvider;
class CodeLensProvider;
class CompletionProvider;
class DefinitionProvider;
class DocumentHighlightProvider;
class DocumentSymbolProvider;
class CallHierarchyProvider;
class InlayHintProvider;
class SignatureHelpProvider;
class DeclarationProvider;
class DocumentLinkProvider;
class ImplementationProvider;
class TypeDefinitionProvider;
class TypeHierarchyProvider;
class FoldingRangeProvider;
class Formatter;
class HoverProvider;
class ReferencesProvider;
class RenameProvider;
class SelectionRangeProvider;
class SemanticTokenProvider;

// Virtual inheritance, so a language that derives both this and its own
// core container holds one `CoreServices` and not two.
struct LspServices : virtual CoreServices {
  explicit LspServices(SharedLspServices& sharedServices) noexcept;
  ~LspServices() noexcept override;

  // The same object as `shared()`, with its LSP type.
  SharedLspServices& lspShared;

  std::unique_ptr<const DocumentSymbolProvider> documentSymbolProvider;
  std::unique_ptr<const DefinitionProvider> definitionProvider;
  // Null by default, down to `codeLensProvider`: only the language knows these
  // answers.
  std::unique_ptr<const DeclarationProvider> declarationProvider;
  std::unique_ptr<const ImplementationProvider> implementationProvider;
  std::unique_ptr<const TypeDefinitionProvider> typeDefinitionProvider;
  std::unique_ptr<const CallHierarchyProvider> callHierarchyProvider;
  std::unique_ptr<const TypeHierarchyProvider> typeHierarchyProvider;
  std::unique_ptr<const SignatureHelpProvider> signatureHelpProvider;
  std::unique_ptr<const InlayHintProvider> inlayHintProvider;
  std::unique_ptr<const DocumentLinkProvider> documentLinkProvider;
  std::unique_ptr<const CodeLensProvider> codeLensProvider;
  // Separate slots: a language may replace rename and keep the stock
  // find-references.
  std::unique_ptr<const ReferencesProvider> referencesProvider;
  std::unique_ptr<const RenameProvider> renameProvider;
  std::unique_ptr<const DocumentHighlightProvider> documentHighlightProvider;
  std::unique_ptr<const SemanticTokenProvider> semanticTokenProvider;
  std::unique_ptr<const FoldingRangeProvider> foldingRangeProvider;
  std::unique_ptr<const SelectionRangeProvider> selectionRangeProvider;
  std::unique_ptr<const HoverProvider> hoverProvider;
  std::unique_ptr<const CompletionProvider> completionProvider;
  // Null by default: without a formatter, no formatting is announced.
  std::unique_ptr<const Formatter> formatter;
  std::unique_ptr<const CodeActionProvider> codeActionProvider;

  void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const override;
  // The LSP rows alone, for a container that combines several sets.
  void visitOwnServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;
};

// Installs the core defaults, then the LSP ones.
void install_default_lsp_services(LspServices& services);

}  // namespace cursiv
