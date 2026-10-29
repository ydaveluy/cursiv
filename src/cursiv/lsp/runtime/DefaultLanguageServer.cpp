#include "cursiv/lsp/runtime/DefaultLanguageServer.hpp"

#include <lsp/messages.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"
#include "cursiv/lsp/actions/CodeActionProvider.hpp"
#include "cursiv/lsp/actions/CodeLensProvider.hpp"
#include "cursiv/lsp/actions/DocumentLinkProvider.hpp"
#include "cursiv/lsp/actions/JsonBridge.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
#include "cursiv/lsp/completion/CompletionProviderOptions.hpp"
#include "cursiv/lsp/content/TextDocumentContentProvider.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/hierarchy/CallHierarchyProvider.hpp"
#include "cursiv/lsp/hierarchy/TypeHierarchyProvider.hpp"
#include "cursiv/lsp/hover/HoverProvider.hpp"
#include "cursiv/lsp/navigation/DeclarationProvider.hpp"
#include "cursiv/lsp/navigation/DefinitionProvider.hpp"
#include "cursiv/lsp/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/lsp/navigation/ImplementationProvider.hpp"
#include "cursiv/lsp/navigation/ReferencesProvider.hpp"
#include "cursiv/lsp/navigation/RenameProvider.hpp"
#include "cursiv/lsp/navigation/TypeDefinitionProvider.hpp"
#include "cursiv/lsp/ranges/FoldingRangeProvider.hpp"
#include "cursiv/lsp/ranges/SelectionRangeProvider.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"
#include "cursiv/lsp/semantic/InlayHintProvider.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/semantic/SignatureHelpProvider.hpp"
#include "cursiv/lsp/services/ExecuteCommandHandler.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/lsp/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/lsp/workspace/ConfigurationProvider.hpp"
#include "cursiv/lsp/workspace/FileOperationHandler.hpp"
#include "lsp/types.h"

namespace cursiv {

namespace {

// The two scales match by name, not by number: `Severity` is the wire
// value, `lsp::DiagnosticSeverity` a zero-based index into the table
// that emits it -- a cast would shift every squiggle one level.
[[nodiscard]] lsp::DiagnosticSeverity to_lsp_severity(
    DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Warning:
      return lsp::DiagnosticSeverity::Warning;
    case DiagnosticSeverity::Information:
      return lsp::DiagnosticSeverity::Information;
    case DiagnosticSeverity::Hint:
      return lsp::DiagnosticSeverity::Hint;
    case DiagnosticSeverity::Error:
      break;
  }
  return lsp::DiagnosticSeverity::Error;
}

// The same name-not-number rule as the severities above: `lsp::DiagnosticTag`
// is a zero-based index and the wire value it emits through is one past it.
[[nodiscard]] lsp::DiagnosticTag to_lsp_tag(DiagnosticTag tag) {
  switch (tag) {
    case DiagnosticTag::Deprecated:
      return lsp::DiagnosticTag::Deprecated;
    case DiagnosticTag::Unnecessary:
      break;
  }
  return lsp::DiagnosticTag::Unnecessary;
}

// Whether a closed document has saved content to fall back to. A URI on a
// scheme the content provider serves has: its text lives in the store, as for
// a builtin library `loadAdditionalDocuments` put there. Otherwise a URI the
// file system provider cannot answer for, such as an `untitled:` buffer under
// the local provider, has none.
[[nodiscard]] bool has_saved_content(const FileSystemProvider& files,
                                     const TextDocumentContentProvider* content,
                                     std::string_view uri) {
  if (content != nullptr) {
    const std::size_t colon = uri.find(':');
    const std::string_view scheme =
        uri.substr(0, colon == std::string_view::npos ? 0 : colon);
    if (!scheme.empty() &&
        std::ranges::any_of(content->schemes(), [&](const std::string& own) {
          return folded_equal(own, scheme);
        }))
      return true;
  }
  try {
    return files.exists(uri);
  } catch (const FileSystemError&) {
    return false;
  }
}

// Whether `options` names `ch`, first or among the others.
[[nodiscard]] bool names_trigger(
    const lsp::DocumentOnTypeFormattingOptions& options, std::string_view ch) {
  if (options.firstTriggerCharacter == ch) return true;
  return options.moreTriggerCharacter.has_value() &&
         std::ranges::find(*options.moreTriggerCharacter, ch) !=
             options.moreTriggerCharacter->end();
}

// The session's on-type options: the first language's as they stand, then
// each character another language names, appended once.
void merge_on_type_options(
    std::optional<lsp::DocumentOnTypeFormattingOptions>& into,
    lsp::DocumentOnTypeFormattingOptions options) {
  if (!into.has_value()) {
    into = std::move(options);
    return;
  }
  const auto add = [&into](std::string& ch) {
    if (names_trigger(*into, ch)) return;
    if (!into->moreTriggerCharacter.has_value())
      into->moreTriggerCharacter.emplace();
    into->moreTriggerCharacter->push_back(std::move(ch));
  };
  add(options.firstTriggerCharacter);
  if (options.moreTriggerCharacter.has_value())
    for (std::string& ch : *options.moreTriggerCharacter) add(ch);
}

// Whether `ch` is one of the characters `formatter` named for on-type
// formatting.
[[nodiscard]] bool formats_on(const Formatter& formatter, std::string_view ch) {
  const std::optional<lsp::DocumentOnTypeFormattingOptions> options =
      formatter.formatOnTypeOptions();
  return options.has_value() && names_trigger(*options, ch);
}

// Whether the first known kind a client lists, its preferred one, is
// markdown. A client that lists none never said it reads markdown.
[[nodiscard]] bool prefers_markdown(
    const std::optional<lsp::Array<lsp::MarkupKindEnum>>& formats) {
  if (formats.has_value())
    for (const lsp::MarkupKindEnum& kind : *formats) {
      if (kind == lsp::MarkupKind::Markdown) return true;
      if (kind == lsp::MarkupKind::PlainText) return false;
    }
  return false;
}

// The providers write their prose as markdown. For a client that prefers
// plain text, the same prose goes as plain text, shown as written.
void as_plain_text(lsp::MarkupContent& content) {
  if (content.kind == lsp::MarkupKind::Markdown)
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::PlainText);
}

}  // namespace

DefaultLanguageServer::~DefaultLanguageServer() noexcept {
  // Only if this is still the installed builder: a replaced one died
  // with its listener list, so there is nothing to unhook from.
  if (_hookedBuilder != nullptr &&
      _shared.documentBuilder.get() == _hookedBuilder)
    _hookedBuilder->removeBuildPhaseListener(*this);
  if (_hookedConfiguration != nullptr &&
      _shared.configurationProvider.get() == _hookedConfiguration)
    _hookedConfiguration->removeConfigurationListener(*this);
}

// At `initialize`, not construction: the constructor only stores the
// reference, and a host may still replace the builder before the
// session starts. This is the first moment the installed builder is
// the session's.
void DefaultLanguageServer::hookServices() {
  DocumentBuilder* const builder = _shared.documentBuilder.get();
  if (builder != _hookedBuilder) {
    builder->addBuildPhaseListener(*this);
    _hookedBuilder = builder;
  }
  ConfigurationProvider* const configuration =
      _shared.configurationProvider.get();
  if (configuration != nullptr && configuration != _hookedConfiguration) {
    configuration->addConfigurationListener(*this);
    _hookedConfiguration = configuration;
  }
}

// Capabilities are read off the installed languages, not declared: an
// absent one is the honest "no", since the protocol reads a missing
// provider as unsupported.
lsp::InitializeResult DefaultLanguageServer::initialize(
    const lsp::InitializeParams& params) {
  hookServices();
  _folders.clear();
  if (params.workspaceFolders.has_value()) {
    if (!params.workspaceFolders->isNull())
      for (const lsp::WorkspaceFolder& folder : params.workspaceFolders->value())
        _folders.push_back({folder.uri.toString(), folder.name});
  } else if (!params.rootUri.isNull()) {
    const std::string uri = params.rootUri->toString();
    _folders.push_back({uri, uri_file_name(uri)});
  } else if (params.rootPath.has_value() && !params.rootPath->isNull() &&
             !params.rootPath->value().empty()) {
    const std::string uri = path_to_file_uri(params.rootPath->value());
    _folders.push_back({uri, uri_file_name(uri)});
  }
  _scanOwed = !_folders.empty();
  // Whether the client will accept the file watcher this server registers at
  // `initialized`. Asked here because this is where the client says it.
  _dynamicFileWatching =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles->dynamicRegistration
          .value_or(false);
  // The other half of the quick fixes' version guard: the server already
  // refuses to build an action measured against a stale version, and
  // this lets the client refuse to apply one that went stale afterward.
  _versionedEdits =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->workspaceEdit.has_value() &&
      params.capabilities.workspace->workspaceEdit->documentChanges.value_or(
          false);
  _markdownHover = false;
  _markdownCompletion = false;
  if (const auto& text = params.capabilities.textDocument) {
    if (text->hover.has_value())
      _markdownHover = prefers_markdown(text->hover->contentFormat);
    if (text->completion.has_value() &&
        text->completion->completionItem.has_value())
      _markdownCompletion = prefers_markdown(
          text->completion->completionItem->documentationFormat);
  }
  if (ConfigurationProvider* configuration =
          _shared.configurationProvider.get())
    configuration->initialize(params);
  for (const auto& callback : _onInitialize) callback(params);

  lsp::InitializeResult result;
  // The options arm, not the bare enum: the numeric form omits
  // `openClose`, and a spec-compliant client would then never send
  // `didOpen`. The save arms stay unset -- no handler here answers them
  // -- and `Incremental` is what `Document::applyEdits` needs.
  lsp::TextDocumentSyncOptions sync;
  sync.openClose = true;
  sync.change =
      lsp::TextDocumentSyncKindEnum(lsp::TextDocumentSyncKind::Incremental);
  result.capabilities.textDocumentSync = sync;
  lsp::ServerInfo info;
  info.name = "cursiv";
  result.serverInfo = std::move(info);
  // The trigger characters are a union, not the last language's list:
  // announced once for the whole session, so keeping only one language's
  // set would leave another's arguments unhelped.
  bool hasSignatureHelp = false;
  std::set<std::string> signatureTriggers;
  std::set<std::string> signatureRetriggers;
  bool hasCompletion = false;
  std::set<std::string> completionTriggers;
  std::set<std::string> completionCommits;
  for (const CoreServices* language : _shared.serviceRegistry->all()) {
    const auto* services = dynamic_cast<const LspServices*>(language);
    if (services == nullptr) continue;
    if (services->documentSymbolProvider != nullptr)
      result.capabilities.documentSymbolProvider = true;
    if (services->definitionProvider != nullptr)
      result.capabilities.definitionProvider = true;
    // The three go-to questions past definition are separate answers, not
    // synonyms: a language installs the ones it can tell apart.
    if (services->declarationProvider != nullptr)
      result.capabilities.declarationProvider = true;
    if (services->typeDefinitionProvider != nullptr)
      result.capabilities.typeDefinitionProvider = true;
    if (services->implementationProvider != nullptr)
      result.capabilities.implementationProvider = true;
    if (services->callHierarchyProvider != nullptr)
      result.capabilities.callHierarchyProvider = lsp::CallHierarchyOptions{};
    if (services->typeHierarchyProvider != nullptr)
      result.capabilities.typeHierarchyProvider = lsp::TypeHierarchyOptions{};
    if (const SignatureHelpProvider* signatures =
            services->signatureHelpProvider.get()) {
      hasSignatureHelp = true;
      const lsp::SignatureHelpOptions options =
          signatures->signatureHelpOptions();
      if (options.triggerCharacters.has_value())
        signatureTriggers.insert(options.triggerCharacters->begin(),
                                 options.triggerCharacters->end());
      if (options.retriggerCharacters.has_value())
        signatureRetriggers.insert(options.retriggerCharacters->begin(),
                                   options.retriggerCharacters->end());
    }
    // `resolveProvider` false on all three: no interface here has a resolve
    // step, so announcing one would invite a request nothing answers.
    if (services->inlayHintProvider != nullptr) {
      lsp::InlayHintOptions options;
      options.resolveProvider = false;
      result.capabilities.inlayHintProvider = options;
    }
    if (services->documentLinkProvider != nullptr) {
      lsp::DocumentLinkOptions options;
      options.resolveProvider = false;
      result.capabilities.documentLinkProvider = options;
    }
    if (services->codeLensProvider != nullptr) {
      lsp::CodeLensOptions options;
      options.resolveProvider = false;
      result.capabilities.codeLensProvider = options;
    }
    if (services->referencesProvider != nullptr)
      result.capabilities.referencesProvider = true;
    if (services->documentHighlightProvider != nullptr)
      result.capabilities.documentHighlightProvider = true;
    // The options arm, not a bare `true`: `prepareProvider` is the only
    // way to say this server also answers `prepareRename`, and without
    // it the client guesses the word boundaries itself.
    if (services->renameProvider != nullptr) {
      lsp::RenameOptions rename;
      rename.prepareProvider = true;
      result.capabilities.renameProvider = rename;
    }
    // The legend travels with the capability: a token's fourth integer
    // indexes `legend.tokenTypes`, unreadable without it. `full` and
    // `range`; no `delta` arm.
    if (services->semanticTokenProvider != nullptr) {
      lsp::SemanticTokensOptions options;
      options.legend = semantic_tokens_legend();
      options.range = true;
      options.full = true;
      result.capabilities.semanticTokensProvider = std::move(options);
    }
    if (services->foldingRangeProvider != nullptr)
      result.capabilities.foldingRangeProvider = true;
    if (services->selectionRangeProvider != nullptr)
      result.capabilities.selectionRangeProvider = true;
    if (services->hoverProvider != nullptr)
      result.capabilities.hoverProvider = true;
    // A union like the signature help above, and for the same reason: the
    // characters are announced once for the whole session.
    if (const CompletionProvider* completion =
            services->completionProvider.get()) {
      hasCompletion = true;
      const CompletionProviderOptions options = completion->completionOptions();
      completionTriggers.insert(options.triggerCharacters.begin(),
                                options.triggerCharacters.end());
      completionCommits.insert(options.allCommitCharacters.begin(),
                               options.allCommitCharacters.end());
    }
    if (services->codeActionProvider != nullptr)
      result.capabilities.codeActionProvider = true;

    // Document and range formatting travel together -- one formatter
    // answers both. On-type is separate and opt-in: it needs the
    // language's trigger character, and a union like the triggers above.
    if (const Formatter* formatter = services->formatter.get()) {
      result.capabilities.documentFormattingProvider = true;
      result.capabilities.documentRangeFormattingProvider = true;
      if (std::optional<lsp::DocumentOnTypeFormattingOptions> onType =
              formatter->formatOnTypeOptions())
        merge_on_type_options(
            result.capabilities.documentOnTypeFormattingProvider,
            std::move(*onType));
    }
  }
  if (hasSignatureHelp) {
    lsp::SignatureHelpOptions options;
    if (!signatureTriggers.empty())
      options.triggerCharacters.emplace(signatureTriggers.begin(),
                                        signatureTriggers.end());
    if (!signatureRetriggers.empty())
      options.retriggerCharacters.emplace(signatureRetriggers.begin(),
                                          signatureRetriggers.end());
    result.capabilities.signatureHelpProvider = std::move(options);
  }
  if (hasCompletion) {
    // `resolveProvider` stays unset: no completion provider of this storey has
    // a resolve step, so announcing one would invite a request nothing answers.
    lsp::CompletionOptions options;
    if (!completionTriggers.empty())
      options.triggerCharacters.emplace(completionTriggers.begin(),
                                        completionTriggers.end());
    if (!completionCommits.empty())
      options.allCommitCharacters.emplace(completionCommits.begin(),
                                          completionCommits.end());
    result.capabilities.completionProvider = std::move(options);
  }
  // Workspace-wide, and asked of the container rather than of a language: one
  // query crosses every language of the session.
  if (const WorkspaceSymbolProvider* symbols =
          _shared.workspaceSymbolProvider.get()) {
    lsp::WorkspaceSymbolOptions options;
    options.resolveProvider = symbols->resolvesSymbols();
    result.capabilities.workspaceSymbolProvider = options;
  }
  // The names are asked once, here: a command the handshake never
  // announced is one no client will ever send.
  if (const ExecuteCommandHandler* commands =
          _shared.executeCommandHandler.get()) {
    lsp::ExecuteCommandOptions options;
    for (std::string& name : commands->commands())
      options.commands.push_back(std::move(name));
    result.capabilities.executeCommandProvider = std::move(options);
  }
  if (const TextDocumentContentProvider* content =
          _shared.textDocumentContentProvider.get()) {
    lsp::TextDocumentContentOptions options;
    const std::span<const std::string> schemes = content->schemes();
    options.schemes.assign(schemes.begin(), schemes.end());
    result.capabilities.workspace = lsp::WorkspaceOptions{};
    result.capabilities.workspace->textDocumentContent = std::move(options);
  }
  // Saying folders are supported is what makes them arrive: untold, a
  // client keeps `workspaceFolders` out of the handshake.
  if (!result.capabilities.workspace.has_value())
    result.capabilities.workspace = lsp::WorkspaceOptions{};
  lsp::WorkspaceFoldersServerCapabilities folders;
  folders.supported = true;
  result.capabilities.workspace->workspaceFolders = std::move(folders);
  // The handler's own options, verbatim: they name the globs a client
  // filters its explorer events against, and the session registers on
  // the same answer. No handler means nothing is said.
  if (const FileOperationHandler* files = _shared.fileOperationHandler.get())
    result.capabilities.workspace->fileOperations =
        files->fileOperationOptions();
  return result;
}

// Configuration first, then the scan; neither is waited for. The
// settings answer comes back on the read loop, so waiting here would be
// this thread waiting for itself.
void DefaultLanguageServer::onInitialize(
    std::function<void(const lsp::InitializeParams&)> callback) {
  _onInitialize.push_back(std::move(callback));
}

void DefaultLanguageServer::initialized() {
  if (ConfigurationProvider* configuration =
          _shared.configurationProvider.get())
    configuration->initialized(_shared.languageClient.get());
  registerFileWatcher();

  // One write action for the whole scan and its publications: the lock
  // serves writes in entry order, so a `didOpen` racing the scan is
  // wholly before or wholly after it, never inside.
  //
  // A bootstrap failure is said, not swallowed: `initialized` is a
  // notification, so an uncaught exception leaves no trace and an empty
  // workspace presents as a healthy one. A cancelled scan never reaches
  // here -- the lock absorbs `OperationCancelled` itself.
  try {
    _shared.workspaceLock->write(
        [&](const CancellationToken& token) {
          _shared.workspaceManager->initializeWorkspace(_folders, token);
        },
        WorkspaceLock::WriteMode::Durable);
  } catch (const std::exception& failure) {
    if (LanguageClient* client = _shared.languageClient.get()) {
      lsp::ShowMessageParams message;
      message.type = lsp::MessageTypeEnum(lsp::MessageType::Error);
      message.message =
          std::string("cursiv: workspace scan failed: ") + failure.what();
      client->showMessage(std::move(message));
    }
  }
}

// `**/*`, not the languages' extensions: a watcher narrowed to one
// suffix would miss directory events -- a rename, a tree deleted --
// which move the most documents. Filtering happens when events arrive.
void DefaultLanguageServer::registerFileWatcher() const {
  LanguageClient* client = _shared.languageClient.get();
  if (client == nullptr || !_dynamicFileWatching) return;
  lsp::FileSystemWatcher watcher;
  watcher.globPattern = lsp::GlobPattern(lsp::Pattern("**/*"));
  lsp::DidChangeWatchedFilesRegistrationOptions options;
  options.watchers.push_back(std::move(watcher));
  lsp::Registration registration;
  registration.id = "cursiv.workspace.didChangeWatchedFiles";
  registration.method =
      std::string(lsp::notifications::WorkspaceDidChangeWatchedFiles::Method);
  lsp::json::Writer writer;
  lsp::writeJson(options, writer);
  registration.registerOptions = lsp::json::parse(writer.text());
  lsp::RegistrationParams params;
  params.registrations.push_back(std::move(registration));
  client->registerCapability(std::move(params));
}

std::optional<lsp::TextDocumentContentResult>
DefaultLanguageServer::textDocumentContent(
    const lsp::TextDocumentContentParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::TextDocumentContentResult> result;
  const TextDocumentContentProvider* provider =
      _shared.textDocumentContentProvider.get();
  if (provider == nullptr) return result;
  if (!awaitScan(token, true)) return result;
  (void)_shared.workspaceLock->read(
      [&]() { result = provider->provideTextDocumentContent(params); }, token);
  return result;
}

// Bytes identical to what the scan already read are not re-installed; the
// build still runs, and carries what a cut build left behind.
void DefaultLanguageServer::didOpen(
    const lsp::DidOpenTextDocumentParams& params) {
  const lsp::TextDocumentItem& item = params.textDocument;
  const std::string uri = item.uri.toString();
  const std::string_view changed[] = {uri};
  Documents& documents = *_shared.documents;
  writeAndPublish(changed, {}, [&]() {
    const std::shared_ptr<Document> existing = documents.getDocument(uri);
    // The language is asked about the uri the document was bound with.
    const std::string normalized = normalize_uri(uri);
    const bool moved =
        existing == nullptr || existing->textDocument().getText() != item.text ||
        existing->language != _shared.serviceRegistry->findServices(
                                  normalized, item.languageId.value());
    if (existing == nullptr) {
      // Parsed once, here: the update keeps an open document's parse, and the
      // build resumes at indexing.
      documents.addDocument(_shared.documentFactory->fromTextDocument(
          TextDocument::create(uri, item.languageId, item.version, item.text)));
    } else if (moved) {
      // The document stays the same object; dropping to `Changed` purges
      // the index rows in the same call that frees the tree -- the one
      // order under which a row's borrowed name stays sound.
      _shared.documentBuilder->resetToState(*existing, DocumentState::Changed);
      _shared.documentFactory->attachTextDocument(
          *existing, TextDocument::create(
                         uri, item.languageId,
                         static_cast<std::int64_t>(item.version), item.text));
    }
    if (const std::shared_ptr<Document> open = documents.getDocument(uri)) {
      open->isOpen = true;
      // The client's number over the scan's zero, whether or not the bytes
      // moved: it is what a diagnostic and a stamped edit report back.
      if (!moved)
        updateVersion(*open, static_cast<std::int64_t>(item.version));
    }
    return moved;
  });
}

// The tree is dropped before the bytes move, on the same document object, so
// index rows are refreshed by the build rather than left dangling. A stale
// version, an empty list, or a byte-identical result moves nothing.
void DefaultLanguageServer::didChange(
    const lsp::DidChangeTextDocumentParams& params) {
  const std::string uri = params.textDocument.uri.toString();
  const auto version = static_cast<std::int64_t>(params.textDocument.version);
  const std::string_view changed[] = {uri};
  writeAndPublish(changed, {}, [&]() {
    Documents& documents = *_shared.documents;
    const std::shared_ptr<Document> document = documents.getDocument(uri);
    // A change on a URI nothing opened is not this server's to guess at.
    if (document == nullptr) return false;
    if (version < document->textDocument().version())
      return false;
    if (params.contentChanges.empty()) {
      updateVersion(*document, version);
      return false;
    }
    // A range outside the text is clamped, not rejected -- the
    // core's `offsetAt` is total. Edits apply to a copy of the
    // text; replacing the document would destroy what the index
    // still describes.
    TextDocument edited = TextDocument::create(
        uri, std::string(document->textDocument().languageId()), version,
        std::string(document->textDocument().getText()));
    TextDocument::update(edited, from_lsp(params.contentChanges), version);
    if (edited.getText() == document->textDocument().getText()) {
      updateVersion(*document, version);
      return false;
    }
    // The rows go before the pool they name; see `didOpen`.
    _shared.documentBuilder->resetToState(*document, DocumentState::Changed);
    _shared.documentFactory->attachTextDocument(*document, std::move(edited));
    return true;
  });
}

void DefaultLanguageServer::updateVersion(Document& document,
                                          std::int64_t version) {
  if (document.textDocument().version() == version) return;
  _shared.documentFactory->attachVersion(document, version);
  // The bytes did not move, so the block stands; the client hears it again
  // under the version it now knows the document by.
  const auto it = _announced.find(document.id);
  if (it != _announced.end() && it->second.count != 0) publish(document);
}

// Closing a tab is not deleting a file: wherever the file lives, its saved
// content replaces the buffer and the document stays, its verdict republished
// by the rebuild. Only a URI with nothing saved behind it leaves -- an
// `untitled:` buffer among them -- and the client is told so.
void DefaultLanguageServer::didClose(
    const lsp::DidCloseTextDocumentParams& params) {
  const std::string uri = params.textDocument.uri.toString();
  const FileSystemProvider& files = *_shared.fileSystemProvider;
  DocumentBuilder& builder = *_shared.documentBuilder;
  Documents& documents = *_shared.documents;
  const std::string_view moved[] = {std::string_view(uri)};
  _shared.workspaceLock->write(
      [&](const CancellationToken& token) {
        const std::shared_ptr<Document> closing = documents.getDocument(uri);
        const bool onDisk =
            closing != nullptr &&
            has_saved_content(files, _shared.textDocumentContentProvider.get(),
                              closing->uri);
        if (closing != nullptr) closing->isOpen = false;
        if (closing == nullptr) {
          builder.update({}, {}, token);
        } else if (onDisk) {
          builder.update(moved, {}, token);
        } else {
          builder.update({}, moved, token);
        }
      },
      WorkspaceLock::WriteMode::Durable);
}

// The store catches up through one build. What this decides is only what
// a naive loop gets wrong: the last event per URI wins (an atomic save is
// a delete+create batch), a delete is re-checked against the file system
// and expanded through the store, and an open document outranks the
// watcher so its buffer is left untouched. Everything else -- statting a
// URI, expanding a directory, dropping what no language claims, creating
// a document the store lacks -- belongs to `DocumentBuilder::update`.
void DefaultLanguageServer::didChangeWatchedFiles(
    const lsp::DidChangeWatchedFilesParams& params) {
  std::vector<std::string> order;
  std::unordered_map<std::string, bool> deletedByUri;
  for (const lsp::FileEvent& event : params.changes) {
    std::string uri = event.uri.toString();
    if (uri.empty()) continue;
    const bool deleted = event.type == lsp::FileChangeType::Deleted;
    if (deletedByUri.insert_or_assign(uri, deleted).second)
      order.push_back(std::move(uri));
  }
  if (order.empty()) return;

  const FileSystemProvider& files = *_shared.fileSystemProvider;
  DocumentBuilder& builder = *_shared.documentBuilder;
  Documents& documents = *_shared.documents;

  // Everything below is one write action, disk reads included: the
  // store decides what an event means, and reading it outside the lock
  // would race whatever write is in flight.
  _shared.workspaceLock->write(
      [&](const CancellationToken& token) {
        std::vector<std::string> changed;
        std::vector<std::string> removed;

        // Classified here, resolved by the build: `DocumentBuilder::update`
        // is what stats a URI, expands a directory, drops what no language
        // claims, and installs a document the store does not hold yet.
        // An open buffer outranks the watcher, and a tracked URI whose file
        // is gone is a removal whatever the event said.
        const auto settle = [&](std::string uri) {
          const std::shared_ptr<Document> existing = documents.getDocument(uri);
          if (existing != nullptr) {
            if (existing->isOpen) return;
            if (!files.exists(existing->uri)) {
              removed.push_back(std::move(uri));
              return;
            }
          }
          changed.push_back(std::move(uri));
        };

        for (const std::string& uri : order) {
          throw_if_cancelled(token);
          if (deletedByUri.at(uri)) {
            // A delete may name a directory: the store's folder index says what
            // was under it.
            if (documents.getDocument(uri) != nullptr) {
              settle(uri);
              continue;
            }
            for (const std::shared_ptr<Document>& d :
                 documents.getDocuments(uri))
              settle(std::string(d->uri));
            continue;
          }
          settle(uri);
        }

        const std::vector<std::string_view> changedUris(changed.begin(),
                                                        changed.end());
        const std::vector<std::string_view> deletedUris(removed.begin(),
                                                        removed.end());

        // One grouped build heals what a created file resolved and
        // invalidates what a deleted one broke; its publish, through the
        // phase hook, sends exactly the documents whose verdict moved.
        builder.update(changedUris, deletedUris, token);

      },
      WorkspaceLock::WriteMode::Durable);
}

// The provider delivers pushed and pulled values through the same listeners.
void DefaultLanguageServer::didChangeConfiguration(
    const lsp::DidChangeConfigurationParams& params) {
  if (ConfigurationProvider* configuration =
          _shared.configurationProvider.get())
    configuration->updateConfiguration(params);
}

void DefaultLanguageServer::onConfigurationSectionUpdate(
    std::string_view section, const lsp::LSPAny&) {
  _shared.workspaceLock->write(
      [&](const CancellationToken& token) {
        DocumentBuilder& builder = *_shared.documentBuilder;
        bool affected = false;
        for (const std::shared_ptr<Document>& document :
             _shared.documents->all()) {
          if (document->language == nullptr ||
              document->language->languageMetaData.languageId != section)
            continue;
          affected = true;
          builder.resetToState(
              *document,
              std::min(document->state.load(), DocumentState::IndexedReferences));
        }
        if (affected) builder.update({}, {}, token);
      },
      WorkspaceLock::WriteMode::Durable);
}

// The three `did` arms are dispatched with no lock: a handler that decides
// to write takes it itself.
void DefaultLanguageServer::didCreateFiles(
    const lsp::CreateFilesParams& params) {
  if (FileOperationHandler* files = _shared.fileOperationHandler.get())
    files->didCreateFiles(params);
}

void DefaultLanguageServer::didRenameFiles(
    const lsp::RenameFilesParams& params) {
  if (FileOperationHandler* files = _shared.fileOperationHandler.get())
    files->didRenameFiles(params);
}

void DefaultLanguageServer::didDeleteFiles(
    const lsp::DeleteFilesParams& params) {
  if (FileOperationHandler* files = _shared.fileOperationHandler.get())
    files->didDeleteFiles(params);
}

// The three `will` arms are read actions, like a command: the edit is computed
// off the workspace and applied by the client, so nothing here mutates.
std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willCreateFiles(
    const lsp::CreateFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willCreateFiles(params); }, token);
  return edit;
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willRenameFiles(
    const lsp::RenameFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willRenameFiles(params); }, token);
  return edit;
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willDeleteFiles(
    const lsp::DeleteFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willDeleteFiles(params); }, token);
  return edit;
}

// `changes` carries what to edit; `documentChanges` adds the version it
// was measured against. This restamps the first as the second for a
// client that announced it can, so a stale click is refused rather
// than applied at the wrong offsets.
void DefaultLanguageServer::stampVersion(lsp::CodeAction& action,
                                         const Document& document) {
  if (!action.edit.has_value() || !action.edit->changes.has_value()) return;
  const lsp::DocumentUri self = lsp::Uri::parse(std::string(document.uri));
  // The protocol mixes file creations and renames into this list; a quick
  // fix only ever edits text, so only that arm is ever built.
  std::vector<std::variant<lsp::TextDocumentEdit, lsp::CreateFile,
                           lsp::RenameFile, lsp::DeleteFile>>
      edits;
  for (auto& [uri, rows] : *action.edit->changes) {
    lsp::TextDocumentEdit one;
    one.textDocument.uri = uri;
    // Only this document's version is known here; another file's edits
    // go out unversioned, which the client reads as "apply as is".
    if (uri == self)
      one.textDocument.version =
          static_cast<int>(document.textDocument().version());
    for (lsp::TextEdit& row : rows) one.edits.emplace_back(std::move(row));
    edits.emplace_back(std::move(one));
  }
  action.edit->changes.reset();
  action.edit->documentChanges = std::move(edits);
}

// The scan resolves `ready` once its documents are in the store, which is the
// one moment a request about one of them can be routed.
bool DefaultLanguageServer::awaitScan(const CancellationToken& token,
                                      bool required) const {
  if (!required && !_scanOwed.load()) return true;
  const std::shared_future<void> ready = _shared.workspaceManager->ready();
  while (ready.wait_for(std::chrono::milliseconds(10)) !=
         std::future_status::ready)
    if (token.stop_requested()) return false;
  return true;
}

// The lookup is a read of the store like any other -- a write rehashes the map
// it walks -- and it is over before the wait, which may not hold the readers'
// side.
bool DefaultLanguageServer::awaitRung(std::string_view uri, DocumentState rung,
                                      const CancellationToken& token) const {
  DocumentId id = kNoDocument;
  if (!_shared.workspaceLock->read(
          [&] { id = _shared.documents->getDocumentId(uri); }, token,
          WorkspaceLock::ReadPriority::Immediate))
    return false;
  return _shared.documentBuilder->waitUntil(rung, id, token) ==
         WaitOutcome::Reached;
}

// A rung that is cancelled, unreachable or lost to a failed build answers
// empty; a cancelled token is turned into the protocol's own refusal by the
// runtime, which checks it after every request.
template <class Service, class Call>
void DefaultLanguageServer::routeDocument(
    std::string_view uri, ServiceRequirement rung,
    std::unique_ptr<const Service> LspServices::* slot,
    const CancellationToken& token, Call call) const {
  const bool workspace = rung.type == ServiceRequirement::Type::Workspace;
  if (!awaitScan(token)) return;
  // A workspace rung is the whole build's, and its read is gated: `Immediate`
  // is admitted at the settle, where documents the write reset are still
  // climbing and their index rows are gone.
  if (workspace) {
    if (_shared.documentBuilder->waitUntil(rung.state, token) !=
        WaitOutcome::Reached)
      return;
  } else if (!awaitRung(uri, rung.state, token)) {
    return;
  }
  // The wait above is the precondition an `Immediate` read owes, so
  // this request no longer queues behind a build past its mutation. The
  // rung is checked again inside the lock, because the wait's verdict
  // is stale by the time of admission -- a write may have settled a
  // fresh document in between. Once admitted the answer is stable:
  // phases only climb, and the next write waits for this reader to
  // leave.
  (void)_shared.workspaceLock->read(
      [&]() {
        const std::shared_ptr<Document> document =
            _shared.documents->getDocument(uri);
        if (document == nullptr) return;
        if (document->state.load() < rung.state) return;
        // The factory fixed the language from both URI and languageId. It may
        // be core-only, in which case this document has no LSP provider.
        const auto* services = dynamic_cast<const LspServices*>(
            document->language);
        if (services == nullptr) return;
        const Service* const provider = (services->*slot).get();
        if (provider == nullptr) return;
        call(*provider, *document);
      },
      token,
      workspace ? WorkspaceLock::ReadPriority::Normal
                : WorkspaceLock::ReadPriority::Immediate);
}

std::vector<lsp::DocumentSymbol> DefaultLanguageServer::documentSymbol(
    const lsp::DocumentSymbolParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentSymbol> symbols;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::documentSymbolProvider, token,
                [&](const DocumentSymbolProvider& provider, const Document& d) {
                  symbols = provider.getSymbols(d, token);
                });
  return symbols;
}

// Nothing is handed over but the document. The target of a definition
// routinely lives in another document, and it carries its own positions --
// including when the client never opened it.
std::vector<lsp::LocationLink> DefaultLanguageServer::definition(
    const lsp::DefinitionParams& params, const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::definitionProvider, token,
                [&](const DefinitionProvider& provider, const Document& d) {
                  if (std::optional<std::vector<lsp::LocationLink>> found =
                          provider.getDefinition(d, params.position, token))
                    links = std::move(*found);
                });
  return links;
}

std::vector<lsp::LocationLink> DefaultLanguageServer::declaration(
    const lsp::DeclarationParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::declarationProvider, token,
                [&](const DeclarationProvider& provider, const Document& d) {
                  if (std::optional<std::vector<lsp::LocationLink>> found =
                          provider.getDeclaration(d, params.position, token))
                    links = std::move(*found);
                });
  return links;
}

std::vector<lsp::LocationLink> DefaultLanguageServer::typeDefinition(
    const lsp::TypeDefinitionParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::typeDefinitionProvider, token,
                [&](const TypeDefinitionProvider& provider, const Document& d) {
                  if (std::optional<std::vector<lsp::LocationLink>> found =
                          provider.getTypeDefinition(d, params.position, token))
                    links = std::move(*found);
                });
  return links;
}

// One rung higher than its neighbours, for the reason find-references is: what
// implements what is read off the inverse index.
std::vector<lsp::LocationLink> DefaultLanguageServer::implementation(
    const lsp::ImplementationParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(),
                WorkspaceState::IndexedReferences,
                &LspServices::implementationProvider, token,
                [&](const ImplementationProvider& provider, const Document& d) {
                  if (std::optional<std::vector<lsp::LocationLink>> found =
                          provider.getImplementation(d, params.position, token))
                    links = std::move(*found);
                });
  return links;
}

std::vector<lsp::Location> DefaultLanguageServer::references(
    const lsp::ReferenceParams& params, const CancellationToken& token) const {
  std::vector<lsp::Location> locations;
  routeDocument(
      params.textDocument.uri.toString(), WorkspaceState::IndexedReferences,
      &LspServices::referencesProvider, token,
      [&](const ReferencesProvider& provider, const Document& d) {
        locations = provider.findReferences(
            d, params.position, params.context.includeDeclaration, token);
      });
  return locations;
}

std::optional<lsp::Range> DefaultLanguageServer::prepareRename(
    const lsp::PrepareRenameParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::Range> span;
  routeDocument(params.textDocument.uri.toString(),
                WorkspaceState::IndexedReferences, &LspServices::renameProvider,
                token, [&](const RenameProvider& provider, const Document& d) {
                  span = provider.prepareRename(d, params.position, token);
                });
  return span;
}

// A rename is a read action, not a slip: it computes edits and hands
// them to the client, which applies them and sends `didChange` back.
// The same holds for the code action and formatting routes below.
std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::rename(
    const lsp::RenameParams& params, const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  routeDocument(params.textDocument.uri.toString(),
                WorkspaceState::IndexedReferences, &LspServices::renameProvider,
                token, [&](const RenameProvider& provider, const Document& d) {
                  edit = provider.rename(d, params.position, params.newName,
                                         token);
                });
  return edit;
}

std::vector<lsp::DocumentHighlight> DefaultLanguageServer::documentHighlight(
    const lsp::DocumentHighlightParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentHighlight> highlights;
  routeDocument(
      params.textDocument.uri.toString(), WorkspaceState::IndexedReferences,
      &LspServices::documentHighlightProvider, token,
      [&](const DocumentHighlightProvider& provider, const Document& d) {
        highlights = provider.getDocumentHighlight(d, params.position, token);
      });
  return highlights;
}

lsp::SemanticTokens DefaultLanguageServer::semanticTokensFull(
    const lsp::SemanticTokensParams& params,
    const CancellationToken& token) const {
  lsp::SemanticTokens tokens;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::semanticTokenProvider, token,
                [&](const SemanticTokenProvider& provider, const Document& d) {
                  tokens = provider.semanticHighlight(d, token);
                });
  return tokens;
}

lsp::SemanticTokens DefaultLanguageServer::semanticTokensRange(
    const lsp::SemanticTokensRangeParams& params,
    const CancellationToken& token) const {
  lsp::SemanticTokens tokens;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::semanticTokenProvider, token,
                [&](const SemanticTokenProvider& provider, const Document& d) {
                  tokens =
                      provider.semanticHighlightRange(d, params.range, token);
                });
  return tokens;
}

std::vector<lsp::InlayHint> DefaultLanguageServer::inlayHint(
    const lsp::InlayHintParams& params, const CancellationToken& token) const {
  std::vector<lsp::InlayHint> hints;
  routeDocument(params.textDocument.uri.toString(),
                DocumentState::IndexedReferences,
                &LspServices::inlayHintProvider, token,
                [&](const InlayHintProvider& provider, const Document& d) {
                  hints = provider.getInlayHints(d, params.range, token);
                });
  return hints;
}

std::vector<lsp::FoldingRange> DefaultLanguageServer::foldingRange(
    const lsp::FoldingRangeParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::FoldingRange> ranges;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::foldingRangeProvider, token,
                [&](const FoldingRangeProvider& provider, const Document& d) {
                  ranges = provider.getFoldingRanges(d, token);
                });
  return ranges;
}

std::vector<lsp::SelectionRange> DefaultLanguageServer::selectionRange(
    const lsp::SelectionRangeParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::SelectionRange> ranges;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::selectionRangeProvider, token,
                [&](const SelectionRangeProvider& provider, const Document& d) {
                  ranges =
                      provider.getSelectionRanges(d, params.positions, token);
                });
  return ranges;
}

std::optional<lsp::Hover> DefaultLanguageServer::hover(
    const lsp::HoverParams& params, const CancellationToken& token) const {
  std::optional<lsp::Hover> hover;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::hoverProvider, token,
                [&](const HoverProvider& provider, const Document& d) {
                  hover = provider.getHoverContent(d, params.position, token);
                });
  if (hover.has_value() && !_markdownHover)
    if (auto* const content = std::get_if<lsp::MarkupContent>(&hover->contents))
      as_plain_text(*content);
  return hover;
}

lsp::CompletionList DefaultLanguageServer::completion(
    const lsp::CompletionParams& params, const CancellationToken& token) const {
  lsp::CompletionList list;
  list.isIncomplete = true;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Linked,
                &LspServices::completionProvider, token,
                [&](const CompletionProvider& provider, const Document& d) {
                  list = provider.getCompletion(d, params.position, token);
                });
  if (!_markdownCompletion)
    for (lsp::CompletionItem& item : list.items)
      if (item.documentation.has_value())
        if (auto* const content =
                std::get_if<lsp::MarkupContent>(&*item.documentation))
          as_plain_text(*content);
  return list;
}

std::optional<lsp::SignatureHelp> DefaultLanguageServer::signatureHelp(
    const lsp::SignatureHelpParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::SignatureHelp> help;
  routeDocument(params.textDocument.uri.toString(),
                DocumentState::IndexedReferences,
                &LspServices::signatureHelpProvider, token,
                [&](const SignatureHelpProvider& provider, const Document& d) {
                  help = provider.provideSignatureHelp(d, params, token);
                });
  return help;
}

// The one route that waits for the top rung: it reads what the validator wrote,
// and asked earlier it would offer fixes for findings not yet made.
std::vector<lsp::CodeAction> DefaultLanguageServer::codeAction(
    const lsp::CodeActionParams& params, const CancellationToken& token) const {
  std::vector<lsp::CodeAction> actions;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Validated,
                &LspServices::codeActionProvider, token,
                [&](const CodeActionProvider& provider, const Document& d) {
                  actions = provider.getCodeActions(d, params, token);
                  if (_versionedEdits)
                    for (lsp::CodeAction& action : actions)
                      stampVersion(action, d);
                });
  return actions;
}

std::vector<lsp::DocumentLink> DefaultLanguageServer::documentLink(
    const lsp::DocumentLinkParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentLink> links;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::documentLinkProvider, token,
                [&](const DocumentLinkProvider& provider, const Document& d) {
                  links = provider.getDocumentLinks(d, token);
                });
  return links;
}

// The top of the resolution rungs, not the parse: a lens routinely counts what
// the workspace points at -- callers, implementers -- and asked earlier it
// would count what was linked so far.
std::vector<lsp::CodeLens> DefaultLanguageServer::codeLens(
    const lsp::CodeLensParams& params, const CancellationToken& token) const {
  std::vector<lsp::CodeLens> lenses;
  routeDocument(params.textDocument.uri.toString(),
                DocumentState::IndexedReferences,
                &LspServices::codeLensProvider, token,
                [&](const CodeLensProvider& provider, const Document& d) {
                  lenses = provider.provideCodeLens(d, token);
                });
  return lenses;
}

// One slot and one rung for the three arms, one call each.
std::vector<lsp::TextEdit> DefaultLanguageServer::formatting(
    const lsp::DocumentFormattingParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::formatter, token,
                [&](const Formatter& formatter, const Document& d) {
                  edits = formatter.formatDocument(d, params, token);
                });
  return edits;
}

std::vector<lsp::TextEdit> DefaultLanguageServer::rangeFormatting(
    const lsp::DocumentRangeFormattingParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::formatter, token,
                [&](const Formatter& formatter, const Document& d) {
                  edits = formatter.formatDocumentRange(d, params, token);
                });
  return edits;
}

// The characters are announced for the whole session, so a request may carry
// another language's: a formatter answers only the ones it named.
std::vector<lsp::TextEdit> DefaultLanguageServer::onTypeFormatting(
    const lsp::DocumentOnTypeFormattingParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  routeDocument(params.textDocument.uri.toString(), DocumentState::Parsed,
                &LspServices::formatter, token,
                [&](const Formatter& formatter, const Document& d) {
                  if (formats_on(formatter, params.ch))
                    edits = formatter.formatDocumentOnType(d, params, token);
                });
  return edits;
}

// The two hierarchies. A preparation is asked at a position, so it routes
// through the document the caret is in; the four expansions carry an item this
// server itself produced, and the URI inside it is what names their document.
std::vector<lsp::CallHierarchyItem> DefaultLanguageServer::prepareCallHierarchy(
    const lsp::CallHierarchyPrepareParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::CallHierarchyItem> items;
  routeDocument(
      params.textDocument.uri.toString(), WorkspaceState::IndexedReferences,
      &LspServices::callHierarchyProvider, token,
      [&](const CallHierarchyProvider& provider, const Document& d) {
        items = provider.prepareCallHierarchy(d, params.position, token);
      });
  return items;
}

std::vector<lsp::CallHierarchyIncomingCall>
DefaultLanguageServer::incomingCalls(
    const lsp::CallHierarchyIncomingCallsParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::CallHierarchyIncomingCall> calls;
  routeDocument(params.item.uri.toString(), WorkspaceState::IndexedReferences,
                &LspServices::callHierarchyProvider, token,
                [&](const CallHierarchyProvider& provider, const Document&) {
                  calls = provider.incomingCalls(params.item, token);
                });
  return calls;
}

std::vector<lsp::CallHierarchyOutgoingCall>
DefaultLanguageServer::outgoingCalls(
    const lsp::CallHierarchyOutgoingCallsParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::CallHierarchyOutgoingCall> calls;
  routeDocument(params.item.uri.toString(), WorkspaceState::IndexedReferences,
                &LspServices::callHierarchyProvider, token,
                [&](const CallHierarchyProvider& provider, const Document&) {
                  calls = provider.outgoingCalls(params.item, token);
                });
  return calls;
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::prepareTypeHierarchy(
    const lsp::TypeHierarchyPrepareParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TypeHierarchyItem> items;
  routeDocument(
      params.textDocument.uri.toString(), WorkspaceState::IndexedReferences,
      &LspServices::typeHierarchyProvider, token,
      [&](const TypeHierarchyProvider& provider, const Document& d) {
        items = provider.prepareTypeHierarchy(d, params.position, token);
      });
  return items;
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::supertypes(
    const lsp::TypeHierarchySupertypesParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TypeHierarchyItem> items;
  routeDocument(params.item.uri.toString(), WorkspaceState::IndexedReferences,
                &LspServices::typeHierarchyProvider, token,
                [&](const TypeHierarchyProvider& provider, const Document&) {
                  items = provider.supertypes(params.item, token);
                });
  return items;
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::subtypes(
    const lsp::TypeHierarchySubtypesParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TypeHierarchyItem> items;
  routeDocument(params.item.uri.toString(), WorkspaceState::IndexedReferences,
                &LspServices::typeHierarchyProvider, token,
                [&](const TypeHierarchyProvider& provider, const Document&) {
                  items = provider.subtypes(params.item, token);
                });
  return items;
}

// A query is about no document, so the rung waited for is the
// workspace's: half an index would silently answer half a query.
std::vector<lsp::WorkspaceSymbol> DefaultLanguageServer::workspaceSymbol(
    const lsp::WorkspaceSymbolParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::WorkspaceSymbol> symbols;
  if (!awaitScan(token)) return symbols;
  if (_shared.documentBuilder->waitUntil(DocumentState::IndexedContent,
                                         token) != WaitOutcome::Reached)
    return symbols;
  (void)_shared.workspaceLock->read(
      [&]() {
        symbols =
            _shared.workspaceSymbolProvider->getSymbols(params.query, token);
      },
      token);
  return symbols;
}

lsp::WorkspaceSymbol DefaultLanguageServer::workspaceSymbolResolve(
    const lsp::WorkspaceSymbol& symbol, const CancellationToken& token) const {
  lsp::WorkspaceSymbol resolved = symbol;
  if (!awaitScan(token)) return resolved;
  if (_shared.documentBuilder->waitUntil(DocumentState::IndexedContent,
                                         token) != WaitOutcome::Reached)
    return resolved;
  (void)_shared.workspaceLock->read(
      [&]() {
        resolved =
            _shared.workspaceSymbolProvider->resolveSymbol(symbol, token);
      },
      token);
  return resolved;
}

// No rung and no lock: a command is the workspace's own code, and takes
// what it needs itself.
std::optional<lsp::LSPAny> DefaultLanguageServer::executeCommand(
    const lsp::ExecuteCommandParams& params,
    const CancellationToken& token) const {
  const ExecuteCommandHandler* handler = _shared.executeCommandHandler.get();
  if (handler == nullptr) return std::nullopt;
  const lsp::LSPArray none;
  const lsp::LSPArray& arguments =
      params.arguments.has_value() ? *params.arguments : none;
  return handler->executeCommand(params.command, arguments, token);
}

// Mutate, then build: one durable write action, publication inside the
// build's drains. The build runs even when nothing moved: it carries what a
// cut build left behind.
void DefaultLanguageServer::writeAndPublish(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted,
    cursiv::function_ref<bool()> mutate) {
  DocumentBuilder& builder = *_shared.documentBuilder;
  _shared.workspaceLock->write(
      [&](const CancellationToken& token) {
        const bool moved = mutate();
        if (moved)
          builder.update(changed, deleted, token);
        else
          builder.update({}, {}, token);
      },
      WorkspaceLock::WriteMode::Durable);
}

void DefaultLanguageServer::onBuildPhase(DocumentState reached,
                                         std::span<Document* const>,
                                         const CancellationToken&) {
  // `Linked` clears, `Validated` publishes; see the declaration.
  if (reached == DocumentState::Linked)
    clearStaleSnapshots();
  else if (reached == DocumentState::Validated)
    publishMovedSnapshots();
}

// Clears squiggles measured against a text that may be gone, the
// moment the workspace is linked, before validation spends its time --
// and only for documents the client was actually shown something for. A removed
// document owes an empty snapshot too, whichever caller removed it.
void DefaultLanguageServer::clearStaleSnapshots() {
  for (auto it = _announced.begin(); it != _announced.end();) {
    const auto announced = it++;
    const std::shared_ptr<Document> d =
        _shared.documents->getDocument(announced->first);
    if (d == nullptr) {
      publishRemoval(_shared.documents->getDocumentUri(announced->first));
      continue;
    }
    if (announced->second.count == 0) continue;  // nothing to clear
    if (!d->diagnostics.empty()) continue;  // verdict intact: not stale
    publish(*d);                            // the empty snapshot
    announced->second = Announced{d->buildRevision, 0};
  }
}

void DefaultLanguageServer::publishMovedSnapshots() {
  for (const std::shared_ptr<Document>& d : _shared.documents->all())
    announceDocument(*d);
}

// Clean is clean whatever the revision did: a document shown no squiggle
// last time owes the client no second empty publication.
void DefaultLanguageServer::announceDocument(const Document& document) {
  const Announced now{document.buildRevision, document.diagnostics.size()};
  const auto it = _announced.find(document.id);
  if (it != _announced.end()) {
    const bool bothClean = it->second.count == 0 && now.count == 0;
    const bool same =
        it->second.revision == now.revision && it->second.count == now.count;
    if (bothClean || same) {
      it->second = now;
      return;
    }
  }
  publish(document);
  _announced[document.id] = now;
}

void DefaultLanguageServer::publish(const Document& document) const {
  LanguageClient* client = _shared.languageClient.get();
  if (client == nullptr) return;  // no session: nothing to notify

  lsp::PublishDiagnosticsParams params;
  params.uri = lsp::Uri::parse(document.uri);
  // The client's version is an `int` on the wire and the store's is unsigned;
  // the one narrowing the protocol imposes happens here and nowhere else.
  params.version = static_cast<int>(document.textDocument().version());

  // Read under the caller's lock: the next build replaces them in place.
  const std::vector<Diagnostic>& block = document.diagnostics;
  {
    params.diagnostics.reserve(block.size());
    for (const Diagnostic& d : block) {
      lsp::Diagnostic published;
      published.range =
          to_lsp(document.textDocument().rangeAt(d.range.begin, d.range.end));
      published.message = d.message;
      published.severity = to_lsp_severity(d.severity);
      if (!d.source.empty()) published.source = std::string(d.source);
      // `DiagnosticCode` is LSP's `integer | string`; the wire narrows the
      // integer to `int` here, the one place the two shapes meet.
      if (d.code.has_value()) {
        if (d.code->isString())
          published.code = d.code->string();
        else
          published.code = static_cast<int>(d.code->integer());
      }
      // The machine payload rides along: LSP round-trips `data` untouched
      // into `codeAction` requests, which is the quick-fix chain's wire.
      if (d.data.has_value()) published.data = to_lsp_any(*d.data);
      // The href, wrapped: `codeDescription` carries only the URL on the core
      // side, and LSP wants it inside a one-field object.
      if (d.codeDescription.has_value()) {
        lsp::CodeDescription description;
        description.href = lsp::Uri::parse(*d.codeDescription);
        published.codeDescription = std::move(description);
      }
      if (!d.tags.empty()) {
        std::vector<lsp::DiagnosticTagEnum> tags;
        tags.reserve(d.tags.size());
        for (const DiagnosticTag tag : d.tags)
          tags.emplace_back(to_lsp_tag(tag));
        published.tags = std::move(tags);
      }
      // Each row names its own document -- an empty `uri` means this
      // one -- and its position is read through that document's own
      // index, never this diagnostic's. An unheld document answers
      // nothing.
      if (!d.relatedInformation.empty()) {
        std::vector<lsp::DiagnosticRelatedInformation> related;
        related.reserve(d.relatedInformation.size());
        for (const DiagnosticRelatedInformation& row : d.relatedInformation) {
          const std::shared_ptr<Document> held =
              row.uri.empty() ? nullptr
                              : _shared.documents->getDocument(row.uri);
          const Document* target = row.uri.empty() ? &document : held.get();
          if (target == nullptr) continue;
          lsp::DiagnosticRelatedInformation info;
          info.location.uri = lsp::Uri::parse(target->uri);
          info.location.range = to_lsp(
              target->textDocument().rangeAt(row.range.begin, row.range.end));
          info.message = row.message;
          related.push_back(std::move(info));
        }
        if (!related.empty()) published.relatedInformation = std::move(related);
      }
      params.diagnostics.push_back(std::move(published));
    }
  }
  client->publishDiagnostics(std::move(params));
}

void DefaultLanguageServer::publishRemoval(std::string_view uri) {
  // The announcement ledger forgets the document too: if the URI is
  // recreated later, its first block is a first announcement again.
  if (const DocumentId id = _shared.documents->getDocumentId(uri);
      id != kNoDocument)
    _announced.erase(id);
  LanguageClient* client = _shared.languageClient.get();
  if (client == nullptr) return;
  lsp::PublishDiagnosticsParams params;
  params.uri = lsp::Uri::parse(uri);
  client->publishDiagnostics(std::move(params));
}

}  // namespace cursiv
