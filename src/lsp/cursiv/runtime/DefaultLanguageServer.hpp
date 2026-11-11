// The lifecycle, wired to the workspace: a notification that changes text is
// one write (mutate, build, publish) and a request is one read. The server
// holds no text of its own.
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
#include <unordered_set>
#include <vector>

#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/text/Range.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceFolder.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"

namespace cursiv {

struct LspServices;

// The state each standard request waits for, in the order of the services
// reference. A host installs its server with other states.
struct RequestStates {
  ServiceRequirement completion = DocumentState::Linked;
  ServiceRequirement hover = DocumentState::Linked;
  ServiceRequirement definition = DocumentState::Linked;
  ServiceRequirement references = WorkspaceState::IndexedReferences;
  ServiceRequirement documentHighlight = WorkspaceState::IndexedReferences;
  // Rename and its preparation.
  ServiceRequirement rename = WorkspaceState::IndexedReferences;
  ServiceRequirement documentSymbol = DocumentState::Parsed;
  ServiceRequirement foldingRange = DocumentState::Parsed;
  ServiceRequirement selectionRange = DocumentState::Parsed;
  // Full, range and delta.
  ServiceRequirement semanticTokens = DocumentState::Linked;
  // Fixes are offered for what validation found.
  ServiceRequirement codeAction = DocumentState::Validated;
  // Whole document, range and on type.
  ServiceRequirement formatting = DocumentState::Parsed;
  ServiceRequirement declaration = DocumentState::Linked;
  ServiceRequirement typeDefinition = DocumentState::Linked;
  // What implements what is read off the inverse index.
  ServiceRequirement implementation = WorkspaceState::IndexedReferences;
  // Each hierarchy's preparation and its two expansions.
  ServiceRequirement callHierarchy = WorkspaceState::IndexedReferences;
  ServiceRequirement typeHierarchy = WorkspaceState::IndexedReferences;
  ServiceRequirement signatureHelp = DocumentState::IndexedReferences;
  ServiceRequirement inlayHint = DocumentState::IndexedReferences;
  ServiceRequirement documentLink = DocumentState::Parsed;
  // A lens counts what the workspace points at, such as callers.
  ServiceRequirement codeLens = DocumentState::IndexedReferences;
  // Search and resolve, awaited on the whole workspace.
  DocumentState workspaceSymbol = DocumentState::IndexedContent;
};

// Publishes diagnostics at each build phase: `Validated` publishes every
// document validated since the last time, clean or not, and `Linked` clears
// the documents removed since. A cancelled build loses nothing.
class DefaultLanguageServer : public LanguageServer,
                              private BuildPhaseListener,
                              private DocumentPhaseListener,
                              private ConfigurationSectionListener {
 public:
  explicit DefaultLanguageServer(SharedLspServices& shared,
                                 RequestStates states = {}) noexcept
      : _shared(shared), _states(states) {}
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
  void willSave(const lsp::WillSaveTextDocumentParams&) override;
  [[nodiscard]] std::vector<lsp::TextEdit> willSaveWaitUntil(
      const lsp::WillSaveTextDocumentParams&,
      const CancellationToken& = {}) const override;
  void didSave(const lsp::DidSaveTextDocumentParams&) override;
  void didOpenNotebookDocument(
      const lsp::DidOpenNotebookDocumentParams&) override;
  void didChangeNotebookDocument(
      const lsp::DidChangeNotebookDocumentParams&) override;
  void didSaveNotebookDocument(
      const lsp::DidSaveNotebookDocumentParams&) override;
  void didCloseNotebookDocument(
      const lsp::DidCloseNotebookDocumentParams&) override;
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
  [[nodiscard]] lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta>
  semanticTokensFullDelta(const lsp::SemanticTokensDeltaParams&,
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

  // What a write's text half moved: rebuilt, or gone from the store.
  struct TextChanges {
    std::vector<std::string> changed;
    std::vector<std::string> deleted;
  };
  // The text half of an open, a change and a close, made inside the write.
  void openText(const lsp::TextDocumentItem& item, TextChanges& changes);
  void changeText(const lsp::VersionedTextDocumentIdentifier& document,
                  std::span<const lsp::TextDocumentContentChangeEvent> edits,
                  TextChanges& changes);
  void closeText(std::string_view uri, TextChanges& changes);

  // The route every request about one document takes: state, lock, document,
  // service, call. The URI is explicit: a hierarchy item names its document.
  template <class Service, class Call>
  void routeDocument(std::string_view uri, ServiceRequirement requirement,
                     std::unique_ptr<const Service> LspServices::* slot,
                     const CancellationToken& token, Call call) const;

  // Waits outside the read, which would block the writer it waits for. False
  // means answer empty: cancelled, excluded from the build, or the build
  // failed.
  [[nodiscard]] bool awaitScan(const CancellationToken&,
                               bool required = false) const;
  [[nodiscard]] bool awaitState(std::string_view uri, DocumentState,
                                const CancellationToken&) const;
  // Runs `read` once every document has reached the state, beside the rest
  // of the build. False means it did not run -- cancelled, or the build
  // failed.
  bool readAtWorkspaceState(DocumentState, const CancellationToken&,
                            cursiv::function_ref<void()> read) const;

  // `mutate` runs in a write and lists what moved, `most` records for texts it
  // installs; a build of them follows, even when `mutate` fails.
  void writeAndPublish(std::size_t most,
                       cursiv::function_ref<void(TextChanges&)> mutate);
  // Every update a write builds. A failure other than a cancellation is shown
  // to the user once until a build validates again, then goes on.
  void buildUpdate(std::span<const std::string_view> changed,
                   std::span<const std::string_view> deleted,
                   const CancellationToken& token);
  void showError(std::string message) const;
  // The notebook receives a notification, then its cells move through
  // `writeAndPublish`.
  void deliverThenWrite(const NotebookDocuments* notebooks,
                        cursiv::function_ref<void()> deliver, std::size_t most,
                        cursiv::function_ref<void(TextChanges&)> mutate);
  // `Linked` clears the documents removed, `Validated` publishes what
  // `onDocumentPhase` recorded.
  void onBuildPhase(DocumentState, std::span<Document* const>,
                    const CancellationToken&) override;
  // Records each validated document, on the worker that validated it: a
  // cancelled build reaches no `Validated` phase, and the next build does not
  // validate its documents again.
  void onDocumentPhase(Document&, DocumentState,
                       const CancellationToken&) override;
  void onConfigurationSectionUpdate(std::string_view,
                                    const lsp::LSPAny&) override;
  void clearRemoved();
  void publishValidated();
  // `publish`, and remember that the client was shown the document.
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
  // Rewrites a `changes` edit as a versioned `documentChanges` one.
  static void addDocumentVersion(lsp::CodeAction& action,
                                 const Document& document);

  SharedLspServices& _shared;
  RequestStates _states;
  // The documents the client was shown a verdict for, until they are gone.
  std::unordered_set<DocumentId> _announced;
  // The documents validated since the last `Validated` phase, written by the
  // validating workers. `_announceAll` stands for a record that was lost: the
  // next drain then announces every validated document.
  std::mutex _validatedMutex;
  std::vector<DocumentId> _validated;
  bool _announceAll = false;
  // Whether the client reads versioned `documentChanges`, which let it refuse
  // a fix clicked after a later keystroke. Set at the handshake.
  bool _versionedEdits = false;
  // Whether the client reads markdown for hover contents and for completion
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
  std::atomic<bool> _scanPending = false;
  // A failed build was shown; a build that validates clears it.
  std::atomic<bool> _buildFailureShown = false;
  // Set at `initialize`, used at `initialized`: nothing is scanned before the
  // client acknowledges the handshake.
  std::vector<WorkspaceFolder> _folders;
  bool _dynamicFileWatching = false;
};

}  // namespace cursiv
