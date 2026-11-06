// The lifecycle, wired to the workspace: every notification that
// changes text is one write action -- mutate, build, publish, in
// order -- and a request is one read action. The server holds no text
// of its own; every route goes through `documents`.
#pragma once

#include <lsp/messages.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/text/Range.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceFolder.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"

namespace cursiv {

struct LspServices;

/**
 * Publishes diagnostics from the build-phase hook: `Linked` clears
 * squiggles a rebuild invalidated, `Validated` publishes every document
 * validated since the last publication, clean or not. A build cancelled
 * mid-way loses nothing: the next one publishes what it validated.
 */
class DefaultLanguageServer : public LanguageServer,
                              private BuildPhaseListener,
                              private DocumentPhaseListener,
                              private ConfigurationSectionListener {
 public:
  explicit DefaultLanguageServer(SharedLspServices& shared) noexcept
      : _shared(shared) {}
  ~DefaultLanguageServer() noexcept override;

  [[nodiscard]] lsp::InitializeResult initialize(
      const lsp::InitializeParams&) override;
  void onInitialize(
      std::function<void(const lsp::InitializeParams&)> callback) override;
  void onRequest(std::string method, CustomRequest handler) override;
  void onNotification(std::string method, CustomNotification handler) override;
  [[nodiscard]] const CustomHandlers& customHandlers() const override {
    return _custom;
  }
  void initialized() override;

  void didOpen(const lsp::DidOpenTextDocumentParams&) override;
  void didChange(const lsp::DidChangeTextDocumentParams&) override;
  void didClose(const lsp::DidCloseTextDocumentParams&) override;
  void didChangeWatchedFiles(const lsp::DidChangeWatchedFilesParams&) override;
  void didChangeConfiguration(
      const lsp::DidChangeConfigurationParams&) override;
  void didChangeWorkspaceFolders(
      const lsp::DidChangeWorkspaceFoldersParams&) override {
    // what folders mean to a workspace is the language's business
  }

  void didCreateFiles(const lsp::CreateFilesParams&) override;
  void didRenameFiles(const lsp::RenameFilesParams&) override;
  void didDeleteFiles(const lsp::DeleteFilesParams&) override;
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> willCreateFiles(
      const lsp::CreateFilesParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> willRenameFiles(
      const lsp::RenameFilesParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> willDeleteFiles(
      const lsp::DeleteFilesParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::DocumentSymbol> documentSymbol(
      const lsp::DocumentSymbolParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::LocationLink> definition(
      const lsp::DefinitionParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::LocationLink> declaration(
      const lsp::DeclarationParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::LocationLink> typeDefinition(
      const lsp::TypeDefinitionParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::LocationLink> implementation(
      const lsp::ImplementationParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::Location> references(
      const lsp::ReferenceParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::optional<lsp::Range> prepareRename(
      const lsp::PrepareRenameParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const lsp::RenameParams&, const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::DocumentHighlight> documentHighlight(
      const lsp::DocumentHighlightParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] lsp::SemanticTokens semanticTokensFull(
      const lsp::SemanticTokensParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] lsp::SemanticTokens semanticTokensRange(
      const lsp::SemanticTokensRangeParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::InlayHint> inlayHint(
      const lsp::InlayHintParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::FoldingRange> foldingRange(
      const lsp::FoldingRangeParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::SelectionRange> selectionRange(
      const lsp::SelectionRangeParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::Hover> hover(
      const lsp::HoverParams&, const CancellationToken& = {}) const override;
  [[nodiscard]] lsp::CompletionList completion(
      const lsp::CompletionParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::SignatureHelp> signatureHelp(
      const lsp::SignatureHelpParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::CodeAction> codeAction(
      const lsp::CodeActionParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::DocumentLink> documentLink(
      const lsp::DocumentLinkParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::CodeLens> codeLens(
      const lsp::CodeLensParams&, const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::CallHierarchyItem> prepareCallHierarchy(
      const lsp::CallHierarchyPrepareParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::CallHierarchyIncomingCall> incomingCalls(
      const lsp::CallHierarchyIncomingCallsParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::CallHierarchyOutgoingCall> outgoingCalls(
      const lsp::CallHierarchyOutgoingCallsParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::TypeHierarchyItem> prepareTypeHierarchy(
      const lsp::TypeHierarchyPrepareParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::TypeHierarchyItem> supertypes(
      const lsp::TypeHierarchySupertypesParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::TypeHierarchyItem> subtypes(
      const lsp::TypeHierarchySubtypesParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::TextEdit> formatting(
      const lsp::DocumentFormattingParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::TextEdit> rangeFormatting(
      const lsp::DocumentRangeFormattingParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::vector<lsp::TextEdit> onTypeFormatting(
      const lsp::DocumentOnTypeFormattingParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::vector<lsp::WorkspaceSymbol> workspaceSymbol(
      const lsp::WorkspaceSymbolParams&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] lsp::WorkspaceSymbol workspaceSymbolResolve(
      const lsp::WorkspaceSymbol&,
      const CancellationToken& = {}) const override;
  [[nodiscard]] std::optional<lsp::LSPAny> executeCommand(
      const lsp::ExecuteCommandParams&,
      const CancellationToken& = {}) const override;

  [[nodiscard]] std::optional<lsp::TextDocumentContentResult>
  textDocumentContent(const lsp::TextDocumentContentParams&,
                      const CancellationToken& = {}) const override;

 private:
  void updateVersion(Document& document, std::int64_t version);

  // The route every request about one document shares: rung, lock,
  // document, slot, call. The URI is explicit because a hierarchy
  // expansion names its document through the item it was handed, not a
  // `textDocument` member.
  template <class Service, class Call>
  void routeDocument(std::string_view uri, ServiceRequirement rung,
                     std::unique_ptr<const Service> LspServices::* slot,
                     const CancellationToken& token, Call call) const;

  // The waits happen outside the read action: waiting under the read
  // would hold the readers' side against the very writer that could
  // satisfy it. False means answer empty -- cancelled, excluded from the
  // build, or the build failed.
  [[nodiscard]] bool awaitScan(const CancellationToken&,
                               bool required = false) const;
  [[nodiscard]] bool awaitRung(std::string_view uri, DocumentState,
                               const CancellationToken&) const;

  // `mutate` runs under exclusivity and reports whether anything moved; a
  // build follows either way, over `changed`/`deleted` through `update`.
  void writeAndPublish(std::span<const std::string_view> changed,
                       std::span<const std::string_view> deleted,
                       cursiv::function_ref<bool()> mutate);
  // `Linked` clears what the rebuild invalidated, `Validated` publishes
  // what `onDocumentPhase` recorded.
  void onBuildPhase(DocumentState, std::span<Document* const>,
                    const CancellationToken&) override;
  // Records each validated document, on the worker that validated it: a
  // cancelled build reaches no `Validated` phase, and the next build does not
  // validate its documents again.
  void onDocumentPhase(Document&, DocumentState,
                       const CancellationToken&) override;
  void onConfigurationSectionUpdate(std::string_view,
                                    const lsp::LSPAny&) override;
  void clearStaleSnapshots();
  void publishValidated();
  // `publish`, and remember how many diagnostics the client now shows.
  void announceDocument(const Document&);
  // Subscribes to the services installed when the session initializes.
  void hookServices();
  void publish(const Document&) const;
  // The empty snapshot with no version: no diagnostics and no document
  // either.
  void publishRemoval(std::string_view uri);
  // The dynamic registration for `workspace/didChangeWatchedFiles`, when
  // the client said it accepts one.
  void registerFileWatcher() const;
  // Restamps a `changes` edit as a versioned `documentChanges` one.
  static void stampVersion(lsp::CodeAction& action, const Document& document);

  SharedLspServices& _shared;
  // How many diagnostics the client was last shown, per document; absent =
  // never published.
  std::unordered_map<DocumentId, std::size_t> _announced;
  // The documents validated since the last `Validated` phase, written by the
  // validating workers.
  std::mutex _validatedMutex;
  std::vector<DocumentId> _validated;
  // Whether the client reads a versioned edit: `documentChanges` carries
  // the version each edit was measured against, letting the client
  // refuse a fix clicked after a later keystroke. Asked at the handshake.
  bool _versionedEdits = false;
  // Whether the client prefers markdown for hover contents and for completion
  // documentation; otherwise that prose goes as plain text. Asked at the
  // handshake.
  bool _markdownHover = false;
  bool _markdownCompletion = false;
  std::vector<std::function<void(const lsp::InitializeParams&)>> _onInitialize;
  CustomHandlers _custom;
  DocumentBuilder* _hookedBuilder = nullptr;
  ConfigurationProvider* _hookedConfiguration = nullptr;
  // Set by a handshake with folders: a request routed before the scan waits
  // for it.
  std::atomic<bool> _scanOwed = false;
  // Arrive at `initialize`, used at `initialized` -- the protocol's own
  // split: nothing may be scanned until the client has acknowledged the
  // handshake. The only state this server keeps between the two.
  std::vector<WorkspaceFolder> _folders;
  bool _dynamicFileWatching = false;
};

}  // namespace cursiv
