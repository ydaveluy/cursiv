// The same shape one storey up: the core container is the base, so a
// language that is both a core language and an LSP one is one object.
// `visitServices` appends this storey's rows to the core's, so
// `firstMissingService` sees both through one scan.
#pragma once

#include <memory>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"

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

  // The same object the base's `shared` names, seen as what it is. Not a
  // second container: retyping is the whole of what it does.
  SharedLspServices& lspShared;

  std::unique_ptr<const DocumentSymbolProvider> documentSymbolProvider;
  std::unique_ptr<const DefinitionProvider> definitionProvider;
  // The three go-to questions past definition. Optional and null by
  // default: what each of them is belongs to the language, and the
  // framework has no answer to install.
  std::unique_ptr<const DeclarationProvider> declarationProvider;
  std::unique_ptr<const ImplementationProvider> implementationProvider;
  std::unique_ptr<const TypeDefinitionProvider> typeDefinitionProvider;
  // The two hierarchies, optional for the same reason: what calls what, and
  // what extends what, are the language's own relations.
  std::unique_ptr<const CallHierarchyProvider> callHierarchyProvider;
  std::unique_ptr<const TypeHierarchyProvider> typeHierarchyProvider;
  // Also the language's own: which construct takes arguments, and what is
  // worth drawing that the author did not write.
  std::unique_ptr<const SignatureHelpProvider> signatureHelpProvider;
  std::unique_ptr<const InlayHintProvider> inlayHintProvider;
  std::unique_ptr<const DocumentLinkProvider> documentLinkProvider;
  // Which line is worth a command, and what the command runs.
  std::unique_ptr<const CodeLensProvider> codeLensProvider;
  // Three slots, not one: a language may want its own rename policy and
  // the stock find-references, and a capability is announced per slot.
  std::unique_ptr<const ReferencesProvider> referencesProvider;
  std::unique_ptr<const RenameProvider> renameProvider;
  std::unique_ptr<const DocumentHighlightProvider> documentHighlightProvider;
  std::unique_ptr<const SemanticTokenProvider> semanticTokenProvider;
  std::unique_ptr<const FoldingRangeProvider> foldingRangeProvider;
  std::unique_ptr<const SelectionRangeProvider> selectionRangeProvider;
  std::unique_ptr<const HoverProvider> hoverProvider;
  std::unique_ptr<const CompletionProvider> completionProvider;
  // The one slot the defaults do not fill: a formatter is the language's
  // own opinion, with no default and no capability announced until one
  // exists.
  std::unique_ptr<const Formatter> formatter;
  std::unique_ptr<const CodeActionProvider> codeActionProvider;

  void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const override;
  /**
   * This storey's rows alone, for a container that combines storeys.
   */
  void visitOwnServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;
};

/**
 * Installs the core defaults, then this storey's. Not templated by the
 * grammar: everything here reads an erased surface -- the name provider,
 * the CST, the reference work list, and the grammar model.
 */
void install_default_lsp_services(LspServices& services);

}  // namespace cursiv
