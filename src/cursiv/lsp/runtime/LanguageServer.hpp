// The lifecycle the editor drives, over the protocol's own generated
// structs so there is no second copy to keep in step. Diagnostics leave
// through `LanguageClient`, never a return value here, which keeps
// publication ordered by the workspace lock.
#pragma once

#include <lsp/messages.h>

#include <functional>
#include <optional>
#include <vector>

#include "cursiv/core/text/Range.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

namespace cursiv {

class LanguageServer {
 public:
  virtual ~LanguageServer() noexcept = default;

  [[nodiscard]] virtual lsp::InitializeResult initialize(
      const lsp::InitializeParams&) = 0;

  // Called with the client's parameters at `initialize`: how a service hears
  // what the client supports. Nothing unsubscribes: a callback stays safe to
  // call once its subscriber is gone.
  virtual void onInitialize(
      std::function<void(const lsp::InitializeParams&)> callback) = 0;

  /**
   * The client's acknowledgement, and the first moment the server may
   * speak. Scans the folders from `initialize`, then asks for settings
   * and the file watching this server needs.
   */
  virtual void initialized() = 0;

  virtual void didOpen(const lsp::DidOpenTextDocumentParams&) = 0;
  virtual void didChange(const lsp::DidChangeTextDocumentParams&) = 0;
  virtual void didClose(const lsp::DidCloseTextDocumentParams&) = 0;

  /**
   * The world moved without the editor: a file written by a build, a
   * branch checked out, a file deleted from a terminal. Re-read through
   * the file system provider, or dropped.
   */
  virtual void didChangeWatchedFiles(
      const lsp::DidChangeWatchedFilesParams&) = 0;

  /**
   * The client's settings changed. Nothing rebuilds here: what a setting
   * means is its reader's business.
   */
  virtual void didChangeConfiguration(
      const lsp::DidChangeConfigurationParams&) = 0;

  // The explorer moved files, not the editor; only the operations the
  // handler announced arrive here.
  virtual void didCreateFiles(const lsp::CreateFilesParams&) = 0;
  virtual void didRenameFiles(const lsp::RenameFilesParams&) = 0;
  virtual void didDeleteFiles(const lsp::DeleteFilesParams&) = 0;

  // Asked before the move; the returned edit is applied first, which is
  // what lets a rename rewrite what names the file. `nullopt` means no
  // change.
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willCreateFiles(
      const lsp::CreateFilesParams&, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willRenameFiles(
      const lsp::RenameFilesParams&, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willDeleteFiles(
      const lsp::DeleteFilesParams&, const CancellationToken& = {}) const = 0;

  // Empty is the answer, never an error, on every route below: an unknown
  // URI, a language with no matching provider, or a position on nothing.
  [[nodiscard]] virtual std::vector<lsp::DocumentSymbol> documentSymbol(
      const lsp::DocumentSymbolParams&,
      const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::LocationLink> definition(
      const lsp::DefinitionParams&, const CancellationToken& = {}) const = 0;

  // The three go-to questions past definition, each its own slot: a
  // language answers only the ones it installs a provider for.
  [[nodiscard]] virtual std::vector<lsp::LocationLink> declaration(
      const lsp::DeclarationParams&, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::LocationLink> typeDefinition(
      const lsp::TypeDefinitionParams&,
      const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::LocationLink> implementation(
      const lsp::ImplementationParams&,
      const CancellationToken& = {}) const = 0;

  // Every place in the workspace naming the symbol at a position; the
  // declaration is included only when the request's context asks for it.
  [[nodiscard]] virtual std::vector<lsp::Location> references(
      const lsp::ReferenceParams&, const CancellationToken& = {}) const = 0;

  // Nothing rather than an error when there is nothing to rename; for
  // `rename`, that also covers a new name the language would reject.
  [[nodiscard]] virtual std::optional<lsp::Range> prepareRename(
      const lsp::PrepareRenameParams&, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> rename(
      const lsp::RenameParams&, const CancellationToken& = {}) const = 0;

  // The same symbol's occurrences in one document, read and write apart.
  [[nodiscard]] virtual std::vector<lsp::DocumentHighlight> documentHighlight(
      const lsp::DocumentHighlightParams&,
      const CancellationToken& = {}) const = 0;
  // `semanticTokens/full` and `/range`; `delta` is not implemented.
  [[nodiscard]] virtual lsp::SemanticTokens semanticTokensFull(
      const lsp::SemanticTokensParams&,
      const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual lsp::SemanticTokens semanticTokensRange(
      const lsp::SemanticTokensRangeParams&,
      const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::InlayHint> inlayHint(
      const lsp::InlayHintParams&, const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::FoldingRange> foldingRange(
      const lsp::FoldingRangeParams&, const CancellationToken& = {}) const = 0;

  // One entry per position of the request, in the same order.
  [[nodiscard]] virtual std::vector<lsp::SelectionRange> selectionRange(
      const lsp::SelectionRangeParams&,
      const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::optional<lsp::Hover> hover(
      const lsp::HoverParams&, const CancellationToken& = {}) const = 0;

  // Always `isIncomplete`: the anchor moves with the next keystroke.
  [[nodiscard]] virtual lsp::CompletionList completion(
      const lsp::CompletionParams&, const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::optional<lsp::SignatureHelp> signatureHelp(
      const lsp::SignatureHelpParams&, const CancellationToken& = {}) const = 0;

  // Empty is "nothing to offer"; the runtime publishes it as null.
  [[nodiscard]] virtual std::vector<lsp::CodeAction> codeAction(
      const lsp::CodeActionParams&, const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::DocumentLink> documentLink(
      const lsp::DocumentLinkParams&, const CancellationToken& = {}) const = 0;

  // Answered complete: there is no `resolve` arm.
  [[nodiscard]] virtual std::vector<lsp::CodeLens> codeLens(
      const lsp::CodeLensParams&, const CancellationToken& = {}) const = 0;

  // The two hierarchies: the client prepares a root once, then expands
  // one level per request from the item it was given -- a URI and a
  // range, never a stored node.
  [[nodiscard]] virtual std::vector<lsp::CallHierarchyItem>
  prepareCallHierarchy(const lsp::CallHierarchyPrepareParams&,
                       const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::CallHierarchyIncomingCall>
  incomingCalls(const lsp::CallHierarchyIncomingCallsParams&,
                const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::CallHierarchyOutgoingCall>
  outgoingCalls(const lsp::CallHierarchyOutgoingCallsParams&,
                const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem>
  prepareTypeHierarchy(const lsp::TypeHierarchyPrepareParams&,
                       const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem> supertypes(
      const lsp::TypeHierarchySupertypesParams&,
      const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem> subtypes(
      const lsp::TypeHierarchySubtypesParams&,
      const CancellationToken& = {}) const = 0;

  // Read actions: the edits go to the client, which applies them and
  // sends the `didChange` back. A document already in shape answers
  // empty, by contract.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> formatting(
      const lsp::DocumentFormattingParams&,
      const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::TextEdit> rangeFormatting(
      const lsp::DocumentRangeFormattingParams&,
      const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::TextEdit> onTypeFormatting(
      const lsp::DocumentOnTypeFormattingParams&,
      const CancellationToken& = {}) const = 0;

  // Not scoped to a document: reads the index, so a symbol in a file
  // nobody opened still answers.
  [[nodiscard]] virtual std::vector<lsp::WorkspaceSymbol> workspaceSymbol(
      const lsp::WorkspaceSymbolParams&,
      const CancellationToken& = {}) const = 0;
  // `workspaceSymbol/resolve`: the symbol back, filled in by a provider that
  // resolves and unchanged by one that does not.
  [[nodiscard]] virtual lsp::WorkspaceSymbol workspaceSymbolResolve(
      const lsp::WorkspaceSymbol&, const CancellationToken& = {}) const = 0;

  // `nullopt` is "no result", sent as null; an unknown command name
  // answers the same way rather than an error.
  [[nodiscard]] virtual std::optional<lsp::LSPAny> executeCommand(
      const lsp::ExecuteCommandParams&,
      const CancellationToken& = {}) const = 0;

  // `workspace/textDocumentContent` (3.18): the text behind a virtual
  // URI, or nothing when no provider knows it.
  [[nodiscard]] virtual std::optional<lsp::TextDocumentContentResult>
  textDocumentContent(const lsp::TextDocumentContentParams&,
                      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
