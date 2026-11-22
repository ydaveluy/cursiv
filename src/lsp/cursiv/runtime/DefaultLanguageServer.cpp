#include "cursiv/runtime/DefaultLanguageServer.hpp"

#include <lsp/messages.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/actions/CodeActionProvider.hpp"
#include "cursiv/actions/CodeLensProvider.hpp"
#include "cursiv/actions/DocumentLinkProvider.hpp"
#include "cursiv/actions/JsonBridge.hpp"
#include "cursiv/completion/CompletionProvider.hpp"
#include "cursiv/completion/CompletionProviderOptions.hpp"
#include "cursiv/content/TextDocumentContentProvider.hpp"
#include "cursiv/formatting/Formatter.hpp"
#include "cursiv/hierarchy/CallHierarchyProvider.hpp"
#include "cursiv/hierarchy/TypeHierarchyProvider.hpp"
#include "cursiv/hover/HoverProvider.hpp"
#include "cursiv/navigation/DeclarationProvider.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/navigation/ImplementationProvider.hpp"
#include "cursiv/navigation/ReferencesProvider.hpp"
#include "cursiv/navigation/RenameProvider.hpp"
#include "cursiv/navigation/TypeDefinitionProvider.hpp"
#include "cursiv/ranges/FoldingRangeProvider.hpp"
#include "cursiv/ranges/SelectionRangeProvider.hpp"
#include "cursiv/runtime/LanguageClient.hpp"
#include "cursiv/semantic/InlayHintProvider.hpp"
#include "cursiv/semantic/SemanticTokenProvider.hpp"
#include "cursiv/semantic/SignatureHelpProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ExecuteCommandHandler.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentSaveHandler.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/FileOperationHandler.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/NotebookDocuments.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"
#include "lsp/types.h"

namespace cursiv {

namespace {

// Whether a closed document has saved content to fall back to: a URI the
// content provider serves, or one the file system provider can answer for.
[[nodiscard]] bool has_saved_content(const FileSystemProvider& files,
                                     const TextDocumentContentProvider* content,
                                     std::string_view uri) {
  if (content != nullptr) {
    const std::size_t colon = uri.find(':');
    const std::string_view scheme =
        uri.substr(0, colon == std::string_view::npos ? 0 : colon);
    if (!scheme.empty() &&
        std::ranges::any_of(content->schemes(), [&](const std::string& own) {
          return foldedEqual(own, scheme);
        }))
      return true;
  }
  try {
    return files.exists(uri);
  } catch (const FileSystemError&) {
    return false;
  }
}

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

[[nodiscard]] bool formats_on(const Formatter& formatter, std::string_view ch) {
  const std::optional<lsp::DocumentOnTypeFormattingOptions> options =
      formatter.formatOnTypeOptions();
  return options.has_value() && names_trigger(*options, ch);
}

// Whether a request arm is announced: `true`, or its options.
template <class Options>
[[nodiscard]] bool announced(
    const std::optional<lsp::OneOf<lsp::Bool, Options>>& arm) {
  if (!arm.has_value()) return false;
  if (const auto* const flag = std::get_if<lsp::Bool>(&*arm)) return *flag;
  return true;
}

[[nodiscard]] bool announces_delta(
    const std::optional<lsp::OneOf<lsp::Bool, lsp::SemanticTokensFullDelta>>&
        full) {
  if (!full.has_value()) return false;
  const auto* const options = std::get_if<lsp::SemanticTokensFullDelta>(&*full);
  return options != nullptr && options->delta.value_or(false);
}

// `names` laid over `into` index by index. The session has one legend, so two
// languages may not give one index two names.
void merge_legend(lsp::Array<lsp::String>& into,
                  const lsp::Array<lsp::String>& names, std::string_view what) {
  for (std::size_t at = 0; at < names.size(); ++at) {
    if (at == into.size())
      into.push_back(names[at]);
    else if (into[at] != names[at])
      throw std::invalid_argument("cursiv: two languages name semantic token " +
                                  std::string(what) + " " + std::to_string(at) +
                                  " '" + into[at] + "' and '" + names[at] +
                                  "'");
  }
}

// The session's semantic tokens: one legend over every language's, and each
// request announced only where every language answers it.
void merge_semantic_tokens_options(
    std::optional<lsp::SemanticTokensOptions>& into,
    const lsp::SemanticTokensOptions& options) {
  if (!into.has_value()) {
    into = options;
    return;
  }
  merge_legend(into->legend.tokenTypes, options.legend.tokenTypes, "type");
  merge_legend(into->legend.tokenModifiers, options.legend.tokenModifiers,
               "modifier");
  if (announced(into->full) && announced(options.full)) {
    lsp::SemanticTokensFullDelta full;
    full.delta = announces_delta(into->full) && announces_delta(options.full);
    into->full = std::move(full);
  } else {
    into->full.reset();
  }
  if (!announced(into->range) || !announced(options.range)) into->range.reset();
}

[[nodiscard]] bool prefers_markdown(
    const std::optional<lsp::Array<lsp::MarkupKindEnum>>& formats) {
  if (formats)
    for (const auto& kind : *formats)
      if (!kind.hasCustomValue()) return kind == lsp::MarkupKind::Markdown;
  return false;
}

// Labels the providers' markdown as plain text, for a client that does not
// read markdown.
void as_plain_text(lsp::MarkupContent& content) {
  if (content.kind == lsp::MarkupKind::Markdown)
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::PlainText);
}

// A `didChange`'s changes in the core's units, strings copied: the params do
// not outlive the call.
std::vector<TextDocumentContentChangeEvent> fromLsp(
    std::span<const lsp::TextDocumentContentChangeEvent> changes) {
  std::vector<TextDocumentContentChangeEvent> converted;
  converted.reserve(changes.size());
  for (const lsp::TextDocumentContentChangeEvent& change : changes) {
    if (const auto* whole =
            std::get_if<lsp::TextDocumentContentChangeWholeDocument>(&change)) {
      converted.emplace_back(std::nullopt, whole->text);
      continue;
    }
    const auto& ranged =
        std::get<lsp::TextDocumentContentChangePartial>(change);
    converted.emplace_back(cursiv::fromLsp(ranged.range), ranged.text);
  }
  return converted;
}

}  // namespace

DefaultLanguageServer::~DefaultLanguageServer() noexcept {
  // Only if this is still the installed builder: a replaced one died
  // with its listener list, so there is nothing to unhook from.
  if (_hookedBuilder != nullptr &&
      _shared.documentBuilder.get() == _hookedBuilder) {
    _hookedBuilder->removeBuildPhaseListener(*this);
    _hookedBuilder->removeDocumentPhaseListener(*this);
  }
  if (_hookedConfiguration != nullptr &&
      _shared.configurationProvider.get() == _hookedConfiguration)
    _hookedConfiguration->removeConfigurationListener(*this);
}

// At `initialize`, not construction: a host may replace the builder until the
// session starts.
void DefaultLanguageServer::hookServices() {
  if (DocumentBuilder* const builder = _shared.documentBuilder.get();
      builder != _hookedBuilder) {
    // Both or neither: the destructor unhooks only a builder it recorded.
    builder->addBuildPhaseListener(*this);
    try {
      builder->addDocumentPhaseListener(*this);
    } catch (...) {
      builder->removeBuildPhaseListener(*this);
      throw;
    }
    _hookedBuilder = builder;
  }
  ConfigurationProvider* const configuration =
      _shared.configurationProvider.get();
  if (configuration != nullptr && configuration != _hookedConfiguration) {
    configuration->addConfigurationListener(*this);
    _hookedConfiguration = configuration;
  }
}

// Capabilities come from the installed languages' services: a missing
// provider reads as unsupported.
lsp::InitializeResult DefaultLanguageServer::initialize(
    const lsp::InitializeParams& params) {
  hookServices();
  _folders.clear();
  if (params.workspaceFolders.has_value()) {
    if (!params.workspaceFolders->isNull())
      for (const lsp::WorkspaceFolder& folder : params.workspaceFolders->value())
        _folders.emplace_back(folder.uri.toString(), folder.name);
  } else if (!params.rootUri.isNull()) {
    const std::string uri = params.rootUri->toString();
    _folders.emplace_back(uri, uriFileName(uri));
  } else if (params.rootPath.has_value() && !params.rootPath->isNull() &&
             !params.rootPath->value().empty()) {
    const std::string uri = pathToFileUri(params.rootPath->value());
    _folders.emplace_back(uri, uriFileName(uri));
  }
  _scanPending = !_folders.empty();
  // Whether the client accepts the file watcher registered at `initialized`.
  _dynamicFileWatching =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles->dynamicRegistration
          .value_or(false);
  // Open documents carry their version in edits the client can check.
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
  // The options form, not the bare enum, whose numeric form omits `openClose`.
  // `Incremental` is what `Document::applyEdits` needs.
  lsp::TextDocumentSyncOptions sync;
  sync.openClose = true;
  sync.change =
      lsp::TextDocumentSyncKindEnum(lsp::TextDocumentSyncKind::Incremental);
  if (const DocumentSaveHandler* saves = _shared.documentSaveHandler.get()) {
    const DocumentSaveOptions options = saves->saveOptions();
    if (options.willSave) sync.willSave = true;
    if (options.willSaveWaitUntil) sync.willSaveWaitUntil = true;
    if (options.didSave) sync.save = true;
  }
  result.capabilities.textDocumentSync = sync;
  if (const NotebookDocuments* notebooks = _shared.notebookDocuments.get())
    result.capabilities.notebookDocumentSync =
        notebooks->notebookDocumentSyncOptions();
  lsp::ServerInfo info;
  info.name = "cursiv";
  result.serverInfo = std::move(info);
  // The trigger characters are the union over all languages: they are
  // announced once per session.
  bool hasSignatureHelp = false;
  std::set<std::string> signatureTriggers;
  std::set<std::string> signatureRetriggers;
  bool hasCompletion = false;
  std::set<std::string> completionTriggers;
  std::set<std::string> completionCommits;
  std::optional<lsp::SemanticTokensOptions> semanticTokens;
  for (const CoreServices* language : _shared.serviceRegistry->all()) {
    const auto* services = dynamic_cast<const LspServices*>(language);
    if (services == nullptr) continue;
    if (services->documentSymbolProvider != nullptr)
      result.capabilities.documentSymbolProvider = true;
    if (services->definitionProvider != nullptr)
      result.capabilities.definitionProvider = true;
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
    // `resolveProvider` stays false: none of the three has a resolve step.
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
    // indexes `legend.tokenTypes`, unreadable without it.
    if (const SemanticTokenProvider* colours =
            services->semanticTokenProvider.get())
      merge_semantic_tokens_options(semanticTokens,
                                    colours->semanticTokensOptions());
    if (services->foldingRangeProvider != nullptr)
      result.capabilities.foldingRangeProvider = true;
    if (services->selectionRangeProvider != nullptr)
      result.capabilities.selectionRangeProvider = true;
    if (services->hoverProvider != nullptr)
      result.capabilities.hoverProvider = true;
    // A union, as for signature help.
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
  if (semanticTokens.has_value())
    result.capabilities.semanticTokensProvider = std::move(*semanticTokens);
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
    // `resolveProvider` stays unset: completion has no resolve step.
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
    const auto& workspace = params.capabilities.workspace;
    _eagerSymbolRanges = *options.resolveProvider;
    if (_eagerSymbolRanges && workspace && workspace->symbol &&
        workspace->symbol->resolveSupport) {
      const auto& properties = workspace->symbol->resolveSupport->properties;
      _eagerSymbolRanges =
          std::ranges::find(properties, "location.range") == properties.end();
    }
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

void DefaultLanguageServer::onRequest(std::string method,
                                      CustomRequest handler) {
  _custom.requests.emplace_back(std::move(method), std::move(handler));
}

void DefaultLanguageServer::onNotification(std::string method,
                                           CustomNotification handler) {
  _custom.notifications.emplace_back(std::move(method), std::move(handler));
}

void DefaultLanguageServer::onInitialize(
    std::function<void(const lsp::InitializeParams&)> callback) {
  _onInitialize.push_back(std::move(callback));
}

// Configuration first, then the scan; neither is waited for. The settings
// answer comes back on the read loop, so waiting here would be this thread
// waiting for itself.
void DefaultLanguageServer::initialized() {
  if (ConfigurationProvider* configuration =
          _shared.configurationProvider.get())
    configuration->initialized(_shared.languageClient.get());
  registerFileWatcher();

  // One write for the whole scan and its publications, so a racing `didOpen`
  // runs wholly before or after it. A bootstrap failure is reported: a
  // notification's exception would leave an empty workspace looking healthy.
  try {
    _shared.workspaceLock->write(
        [&](const CancellationToken& token) {
          _shared.workspaceManager->initializeWorkspace(_folders, token);
        },
        WorkspaceLock::WriteMode::Protected);
  } catch (const std::exception& failure) {
    showError(std::string("cursiv: workspace scan failed: ") + failure.what());
  }
}

void DefaultLanguageServer::showError(std::string message) const {
  if (LanguageClient* client = _shared.languageClient.get()) {
    lsp::ShowMessageParams params;
    params.type = lsp::MessageTypeEnum(lsp::MessageType::Error);
    params.message = std::move(message);
    client->showMessage(std::move(params));
  }
}

// A glob for every file, not the languages' extensions: a watcher narrowed to
// one suffix would miss directory events -- a rename, a tree deleted -- which
// move the most documents. Filtering happens when events arrive.
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

void DefaultLanguageServer::didOpen(
    const lsp::DidOpenTextDocumentParams& params) {
  writeAndPublish(
      1, [&](TextChanges& changes) { openText(params.textDocument, changes); });
}

void DefaultLanguageServer::didChange(
    const lsp::DidChangeTextDocumentParams& params) {
  writeAndPublish(1, [&](TextChanges& changes) {
    changeText(params.textDocument, params.contentChanges, changes);
  });
}

void DefaultLanguageServer::didClose(
    const lsp::DidCloseTextDocumentParams& params) {
  writeAndPublish(1, [&](TextChanges& changes) {
    closeText(params.textDocument.uri.toString(), changes);
  });
}

// Bytes identical to what the scan already read are not re-installed; the
// build still runs, and carries what a cut build left behind.
void DefaultLanguageServer::openText(const lsp::TextDocumentItem& item,
                                     TextChanges& changes) {
  std::string uri = item.uri.toString();
  Documents& documents = *_shared.documents;
  auto document = documents.getDocument(uri);
  if (document && document->textDocument().getText() == item.text &&
      document->language == _shared.serviceRegistry->findServices(
                                document->uri, item.languageId.value())) {
    document->isOpen = true;
    updateVersion(*document, item.version);
    return;
  }
  changes.insert_or_assign(uri, false);
  auto text = TextDocument::create(std::move(uri), item.languageId,
                                   item.version, item.text);
  if (document) {
    _shared.documentFactory->attachTextDocument(*document, std::move(text));
  } else {
    document = _shared.documentFactory->create(std::move(text));
    documents.addDocument(document);
  }
  document->isOpen = true;
}

// The text is edited in place on the same document object, its tree dropped
// first, so index rows are refreshed by the build. A stale version moves
// nothing; no change, or a lone one rewriting its range alike, only versions.
void DefaultLanguageServer::changeText(
    const lsp::VersionedTextDocumentIdentifier& identifier,
    std::span<const lsp::TextDocumentContentChangeEvent> edits,
    TextChanges& changes) {
  std::string uri = identifier.uri.toString();
  const auto version = static_cast<std::int64_t>(identifier.version);
  const std::shared_ptr<Document> document =
      _shared.documents->getDocument(uri);
  if (!document || !document->isOpen ||
      version <= document->textDocument().version())
    return;
  // A range outside the text is clamped, not rejected -- the core's
  // `offsetAt` is total.
  if (!_shared.documentFactory->updateTextDocument(*document, fromLsp(edits),
                                                   version)) {
    updateVersion(*document, version);
    return;
  }
  changes.insert_or_assign(std::move(uri), false);
}

void DefaultLanguageServer::updateVersion(Document& document,
                                          std::int64_t version) {
  if (document.textDocument().version() == version) return;
  _shared.documentFactory->attachVersion(document, version);
  // The same bytes keep their diagnostics under the client's new version.
  if (document.buildFailed || document.state.load() == DocumentState::Validated)
    announceDocument(document);
}

// Closing a tab is not deleting a file: the saved content replaces the buffer
// and the document stays. Only a URI with nothing saved behind it, such as an
// `untitled:` buffer or a notebook cell, leaves.
void DefaultLanguageServer::closeText(std::string_view uri,
                                      TextChanges& changes) const {
  const std::shared_ptr<Document> closing = _shared.documents->getDocument(uri);
  if (closing == nullptr) return;
  const bool saved = has_saved_content(
      *_shared.fileSystemProvider, _shared.textDocumentContentProvider.get(),
      closing->uri);
  changes.insert_or_assign(std::string(uri), !saved);
  closing->isOpen = false;
}

// The handler is told with no lock held, as for the file operations: one that
// decides to read or write takes the lock itself.
void DefaultLanguageServer::willSave(
    const lsp::WillSaveTextDocumentParams& params) {
  if (DocumentSaveHandler* saves = _shared.documentSaveHandler.get())
    saves->willSaveDocument(params);
}

void DefaultLanguageServer::didSave(
    const lsp::DidSaveTextDocumentParams& params) {
  if (DocumentSaveHandler* saves = _shared.documentSaveHandler.get())
    saves->didSaveDocument(params);
}

// A read action, like the file operations' `will` arms: the client applies the
// edits, then saves.
std::vector<lsp::TextEdit> DefaultLanguageServer::willSaveWaitUntil(
    const lsp::WillSaveTextDocumentParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  const DocumentSaveHandler* saves = _shared.documentSaveHandler.get();
  if (saves == nullptr) return edits;
  const std::string uri = params.textDocument.uri.toString();
  (void)_shared.workspaceLock->read(
      [&]() {
        const std::shared_ptr<Document> document =
            _shared.documents->getDocument(uri);
        if (document != nullptr && document->isOpen)
          edits = saves->willSaveDocumentWaitUntil(*document, params, token);
      },
      token);
  return edits;
}

// The notebook receives first, so a build of its cells sees it as the client
// has it; then the cells move in one write. The write runs even when nothing
// moves: its build carries what the build it cut left behind.
void DefaultLanguageServer::didOpenNotebookDocument(
    const lsp::DidOpenNotebookDocumentParams& params) {
  NotebookDocuments* notebooks = _shared.notebookDocuments.get();
  deliverThenWrite(
      notebooks, [&] { notebooks->didOpen(params); },
      params.cellTextDocuments.size(),
      [&](TextChanges& changes) {
        for (const lsp::TextDocumentItem& cell : params.cellTextDocuments)
          openText(cell, changes);
      });
}

void DefaultLanguageServer::didChangeNotebookDocument(
    const lsp::DidChangeNotebookDocumentParams& params) {
  NotebookDocuments* notebooks = _shared.notebookDocuments.get();
  const std::optional<lsp::NotebookDocumentCellChanges>& cells =
      params.change.cells;
  std::size_t records = 0;
  if (cells.has_value()) {
    if (cells->structure.has_value()) {
      const auto& structure = *cells->structure;
      if (structure.didOpen.has_value()) records += structure.didOpen->size();
      if (structure.didClose.has_value()) records += structure.didClose->size();
    }
    if (cells->textContent.has_value()) records += cells->textContent->size();
  }
  bool accepted = true;
  deliverThenWrite(
      notebooks, [&] { accepted = notebooks->didChange(params); }, records,
      [&](TextChanges& changes) {
        if (!accepted || !cells.has_value()) return;
        if (cells->structure.has_value()) {
          if (cells->structure->didOpen.has_value())
            for (const lsp::TextDocumentItem& cell : *cells->structure->didOpen)
              openText(cell, changes);
          if (cells->structure->didClose.has_value())
            for (const lsp::TextDocumentIdentifier& cell :
                 *cells->structure->didClose)
              closeText(cell.uri.toString(), changes);
        }
        if (cells->textContent.has_value())
          for (const lsp::NotebookDocumentCellContentChanges& cell :
               *cells->textContent)
            changeText(cell.document, cell.changes, changes);
      });
}

void DefaultLanguageServer::didSaveNotebookDocument(
    const lsp::DidSaveNotebookDocumentParams& params) {
  if (NotebookDocuments* notebooks = _shared.notebookDocuments.get())
    notebooks->didSave(params);
}

void DefaultLanguageServer::didCloseNotebookDocument(
    const lsp::DidCloseNotebookDocumentParams& params) {
  NotebookDocuments* notebooks = _shared.notebookDocuments.get();
  deliverThenWrite(
      notebooks, [&] { notebooks->didClose(params); },
      params.cellTextDocuments.size(),
      [&](TextChanges& changes) {
        for (const lsp::TextDocumentIdentifier& cell : params.cellTextDocuments)
          closeText(cell.uri.toString(), changes);
      });
}

// Without a notebook service no cell moves. A notebook that fails to receive
// the notification still lets its cells move and build, then its failure goes
// on.
void DefaultLanguageServer::deliverThenWrite(
    const NotebookDocuments* notebooks, cursiv::FunctionRef<void()> deliver,
    std::size_t most, cursiv::FunctionRef<void(TextChanges&)> mutate) {
  std::exception_ptr failure;
  if (notebooks != nullptr) {
    try {
      deliver();
    } catch (...) {
      failure = std::current_exception();
    }
  }
  writeAndPublish(most, [&](TextChanges& changes) {
    if (notebooks != nullptr) mutate(changes);
  });
  if (failure) std::rethrow_exception(failure);
}

// The store catches up through one build: the last event per URI wins, a
// delete is checked against the file system, and an open document's buffer
// is kept. Classified in the write, disk reads included: reading the store
// outside the lock would race whatever write is in flight.
void DefaultLanguageServer::didChangeWatchedFiles(
    const lsp::DidChangeWatchedFilesParams& params) {
  const FileSystemProvider& files = *_shared.fileSystemProvider;
  const Documents& documents = *_shared.documents;
  writeAndPublish(params.changes.size(), [&](TextChanges& changes) {
    std::vector<std::string> order;
    std::unordered_map<std::string, bool> deletedByUri;
    for (const lsp::FileEvent& event : params.changes) {
      std::string uri = event.uri.toString();
      if (uri.empty()) continue;
      const bool deleted = event.type == lsp::FileChangeType::Deleted;
      if (deletedByUri.insert_or_assign(uri, deleted).second)
        order.push_back(std::move(uri));
    }

    // Resolved by `DocumentBuilder::update`. An open buffer outranks the
    // watcher, and a tracked URI whose file is gone is removed.
    const auto classify = [&](std::string uri) {
      if (const std::shared_ptr<Document> existing = documents.getDocument(uri);
          existing != nullptr) {
        if (existing->isOpen) return;
        if (!files.exists(existing->uri)) {
          changes.insert_or_assign(std::move(uri), true);
          return;
        }
      }
      changes.insert_or_assign(std::move(uri), false);
    };

    for (const std::string& uri : order) {
      if (deletedByUri.at(uri)) {
        // A delete may name a directory: the store's folder index says what
        // was under it.
        if (documents.getDocument(uri) != nullptr) {
          classify(uri);
          continue;
        }
        for (const std::shared_ptr<Document>& d : documents.getDocuments(uri))
          classify(std::string(d->uri));
        continue;
      }
      classify(uri);
    }
  });
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
        if (affected) buildUpdate({}, {}, token);
      },
      WorkspaceLock::WriteMode::Protected);
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

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::readFileEdit(
    const CancellationToken& token,
    FunctionRef<std::optional<lsp::WorkspaceEdit>(const FileOperationHandler&)>
        request) const {
  std::optional<lsp::WorkspaceEdit> edit;
  if (const auto* files = _shared.fileOperationHandler.get())
    (void)_shared.workspaceLock->read(
        [&] {
          edit = request(*files);
          addDocumentVersions(edit);
        },
        token);
  return edit;
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willCreateFiles(
    const lsp::CreateFilesParams& params,
    const CancellationToken& token) const {
  return readFileEdit(
      token, [&](const auto& files) { return files.willCreateFiles(params); });
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willRenameFiles(
    const lsp::RenameFilesParams& params,
    const CancellationToken& token) const {
  return readFileEdit(
      token, [&](const auto& files) { return files.willRenameFiles(params); });
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willDeleteFiles(
    const lsp::DeleteFilesParams& params,
    const CancellationToken& token) const {
  return readFileEdit(
      token, [&](const auto& files) { return files.willDeleteFiles(params); });
}

// Capture the open targets' versions under the same read lock as their edits.
void DefaultLanguageServer::addDocumentVersions(
    std::optional<lsp::WorkspaceEdit>& edit) const {
  if (!_versionedEdits || !edit || !edit->changes || edit->documentChanges)
    return;
  auto& edits = edit->documentChanges.emplace();
  edits.reserve(edit->changes->size());
  for (auto& [uri, rows] : *edit->changes) {
    lsp::TextDocumentEdit one;
    one.textDocument.uri = uri;
    if (const auto target = _shared.documents->getDocument(uri.toString());
        target && target->isOpen)
      one.textDocument.version =
          static_cast<int>(target->textDocument().version());
    one.edits.reserve(rows.size());
    for (lsp::TextEdit& row : rows) one.edits.emplace_back(std::move(row));
    edits.emplace_back(std::move(one));
  }
  edit->changes.reset();
}

// The scan resolves `ready` once its documents are in the store, which is the
// one moment a request about one of them can be routed.
bool DefaultLanguageServer::awaitScan(const CancellationToken& token,
                                      bool required) const {
  if (!required && !_scanPending.load()) return true;
  return _shared.workspaceManager->waitUntilReady(token);
}

// A state that is cancelled, unreachable or lost to a failed build answers
// empty; the runtime turns a cancelled token into the protocol's refusal.
void DefaultLanguageServer::readDocument(
    std::string_view uri, ServiceRequirement requirement, const CancellationToken& token,
    FunctionRef<void(const LspServices&, const Document&)> read) const {
  const bool workspace =
      requirement.type == ServiceRequirement::Type::Workspace;
  if (!awaitScan(token)) return;
  // False when a later write reset the document after the wait: it is then
  // awaited again. A document below a workspace state the workspace reached was
  // left out of the build, which answers empty.
  DocumentId id = kNoDocument;
  const auto answer = [&]() {
    if (id == kNoDocument) id = _shared.documents->getDocumentId(uri);
    const auto document = _shared.documents->getDocument(id);
    if (document == nullptr) return true;
    if (document->state.load() < requirement.state) return workspace;
    // The factory chose the language from both URI and languageId. It may
    // be core-only, in which case this document has no LSP provider.
    const auto* services = dynamic_cast<const LspServices*>(document->language);
    if (services) read(*services, *document);
    return true;
  };
  if (workspace) {
    (void)readAtWorkspaceState(requirement.state, token,
                               [&] { (void)answer(); });
    return;
  }
  // An id survives document replacement; resolve it under the lock, then wait outside it.
  if (!_shared.workspaceLock->read([&] { id = _shared.documents->getDocumentId(uri); }, token,
                                   WorkspaceLock::ReadPriority::Immediate)) return;
  while (_shared.documentBuilder->waitUntil(requirement.state, id, token) == WaitOutcome::Reached) {
    bool answered = false;
    if (!_shared.workspaceLock->read([&] { answered = answer(); }, token,
                                     WorkspaceLock::ReadPriority::Immediate) || answered) return;
  }
}

// A workspace state is the builder's, not a document's. It is checked again
// inside the lock, where a write may have lowered it, and then awaited again.
bool DefaultLanguageServer::readAtWorkspaceState(
    DocumentState state, const CancellationToken& token,
    cursiv::FunctionRef<void()> read) const {
  const DocumentBuilder& builder = *_shared.documentBuilder;
  for (bool lowered = true; lowered;) {
    if (builder.waitUntil(state, token) != WaitOutcome::Reached) return false;
    if (!_shared.workspaceLock->read(
            [&] {
              lowered = builder.currentState() < state;
              if (!lowered) read();
            },
            token, WorkspaceLock::ReadPriority::Immediate))
      return false;
  }
  return true;
}

template <class Service, class Result, class... Params, class... Args>
Result DefaultLanguageServer::routeDocument(
    const lsp::DocumentUri& uri, ServiceRequirement requirement, const CancellationToken& token,
    std::unique_ptr<const Service> LspServices::* slot, Result (Service::*method)(Params...) const,
    const Args&... args) const {
  Result result{};
  readDocument(uri.toString(), requirement, token, [&](const LspServices& services, const Document& d) {
    if (const auto& provider = services.*slot) {
      if constexpr (std::is_invocable_v<decltype(method), const Service&, const Document&,
                                        const Args&..., const CancellationToken&>)
        result = ((*provider).*method)(d, args..., token);
      else
        result = ((*provider).*method)(args..., token);
    }
  });
  return result;
}

std::vector<lsp::DocumentSymbol> DefaultLanguageServer::documentSymbol(
    const lsp::DocumentSymbolParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.documentSymbol, token,
                       &LspServices::documentSymbolProvider, &DocumentSymbolProvider::getSymbols);
}

// The provider gets only the asking document: a target in another document,
// even one the client never opened, carries its own positions.
std::vector<lsp::LocationLink> DefaultLanguageServer::definition(
    const lsp::DefinitionParams& params, const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.definition, token,
                       &LspServices::definitionProvider, &DefinitionProvider::getDefinition,
                       params.position)
      .value_or(std::vector<lsp::LocationLink>{});
}

std::vector<lsp::LocationLink> DefaultLanguageServer::declaration(
    const lsp::DeclarationParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.declaration, token,
                       &LspServices::declarationProvider, &DeclarationProvider::getDeclaration,
                       params.position)
      .value_or(std::vector<lsp::LocationLink>{});
}

std::vector<lsp::LocationLink> DefaultLanguageServer::typeDefinition(
    const lsp::TypeDefinitionParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.typeDefinition, token,
                       &LspServices::typeDefinitionProvider, &TypeDefinitionProvider::getTypeDefinition,
                       params.position)
      .value_or(std::vector<lsp::LocationLink>{});
}

std::vector<lsp::LocationLink> DefaultLanguageServer::implementation(
    const lsp::ImplementationParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.implementation, token,
                       &LspServices::implementationProvider, &ImplementationProvider::getImplementation,
                       params.position)
      .value_or(std::vector<lsp::LocationLink>{});
}

std::vector<lsp::Location> DefaultLanguageServer::references(
    const lsp::ReferenceParams& params, const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.references, token,
                       &LspServices::referencesProvider, &ReferencesProvider::findReferences,
                       params.position, params.context.includeDeclaration);
}

std::optional<lsp::Range> DefaultLanguageServer::prepareRename(
    const lsp::PrepareRenameParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.rename, token,
                       &LspServices::renameProvider, &RenameProvider::prepareRename,
                       params.position);
}

// A rename is a read action: the client applies the edits and sends
// `didChange` back. So are the code action and formatting routes below.
std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::rename(
    const lsp::RenameParams& params, const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  readDocument(params.textDocument.uri.toString(), _states.rename, token,
               [&](const LspServices& services, const Document& d) {
                 if (const auto& provider = services.renameProvider) {
                   edit = provider->rename(d, params.position, params.newName,
                                          token);
                   addDocumentVersions(edit);
                 }
               });
  return edit;
}

std::vector<lsp::DocumentHighlight> DefaultLanguageServer::documentHighlight(
    const lsp::DocumentHighlightParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.documentHighlight, token,
                       &LspServices::documentHighlightProvider, &DocumentHighlightProvider::getDocumentHighlight,
                       params.position);
}

lsp::SemanticTokens DefaultLanguageServer::semanticTokensFull(
    const lsp::SemanticTokensParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.semanticTokens, token,
                       &LspServices::semanticTokenProvider, &SemanticTokenProvider::semanticHighlight,
                       params);
}

lsp::SemanticTokens DefaultLanguageServer::semanticTokensRange(
    const lsp::SemanticTokensRangeParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.semanticTokens, token,
                       &LspServices::semanticTokenProvider, &SemanticTokenProvider::semanticHighlightRange,
                       params);
}

// A document nothing colours answers the empty tokens whole, as `full` does.
lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta>
DefaultLanguageServer::semanticTokensFullDelta(
    const lsp::SemanticTokensDeltaParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.semanticTokens, token,
                       &LspServices::semanticTokenProvider, &SemanticTokenProvider::semanticHighlightDelta,
                       params);
}

std::vector<lsp::InlayHint> DefaultLanguageServer::inlayHint(
    const lsp::InlayHintParams& params, const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.inlayHint, token,
                       &LspServices::inlayHintProvider, &InlayHintProvider::getInlayHints,
                       params.range);
}

std::vector<lsp::FoldingRange> DefaultLanguageServer::foldingRange(
    const lsp::FoldingRangeParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.foldingRange, token,
                       &LspServices::foldingRangeProvider, &FoldingRangeProvider::getFoldingRanges);
}

std::vector<lsp::SelectionRange> DefaultLanguageServer::selectionRange(
    const lsp::SelectionRangeParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.selectionRange, token,
                       &LspServices::selectionRangeProvider, &SelectionRangeProvider::getSelectionRanges,
                       params.positions);
}

std::optional<lsp::Hover> DefaultLanguageServer::hover(
    const lsp::HoverParams& params, const CancellationToken& token) const {
  auto hover = routeDocument(params.textDocument.uri, _states.hover, token,
                       &LspServices::hoverProvider, &HoverProvider::getHoverContent,
                       params.position);
  if (hover.has_value() && !_markdownHover)
    if (auto* const content = std::get_if<lsp::MarkupContent>(&hover->contents))
      as_plain_text(*content);
  return hover;
}

lsp::CompletionList DefaultLanguageServer::completion(
    const lsp::CompletionParams& params, const CancellationToken& token) const {
  lsp::CompletionList list;
  list.isIncomplete = true;
  readDocument(params.textDocument.uri.toString(), _states.completion, token,
               [&](const LspServices& services, const Document& d) {
                 if (const auto& provider = services.completionProvider) {
                   list = provider->getCompletion(d, params, token);
                 }
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
  return routeDocument(params.textDocument.uri, _states.signatureHelp, token,
                       &LspServices::signatureHelpProvider, &SignatureHelpProvider::provideSignatureHelp,
                       params);
}

std::vector<lsp::CodeAction> DefaultLanguageServer::codeAction(
    const lsp::CodeActionParams& params, const CancellationToken& token) const {
  std::vector<lsp::CodeAction> actions;
  readDocument(params.textDocument.uri.toString(), _states.codeAction, token,
               [&](const LspServices& services, const Document& d) {
                 if (const auto& provider = services.codeActionProvider) {
                   actions = provider->getCodeActions(d, params, token);
                   for (auto& action : actions) addDocumentVersions(action.edit);
                 }
               });
  return actions;
}

std::vector<lsp::DocumentLink> DefaultLanguageServer::documentLink(
    const lsp::DocumentLinkParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.documentLink, token,
                       &LspServices::documentLinkProvider, &DocumentLinkProvider::getDocumentLinks);
}

std::vector<lsp::CodeLens> DefaultLanguageServer::codeLens(
    const lsp::CodeLensParams& params, const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.codeLens, token,
                       &LspServices::codeLensProvider, &CodeLensProvider::provideCodeLens);
}

// One slot and one state for the three arms, one call each.
std::vector<lsp::TextEdit> DefaultLanguageServer::formatting(
    const lsp::DocumentFormattingParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.formatting, token,
                       &LspServices::formatter, &Formatter::formatDocument,
                       params);
}

std::vector<lsp::TextEdit> DefaultLanguageServer::rangeFormatting(
    const lsp::DocumentRangeFormattingParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.formatting, token,
                       &LspServices::formatter, &Formatter::formatDocumentRange,
                       params);
}

// The characters are announced for the whole session, so a request may carry
// another language's: a formatter answers only the ones it named.
std::vector<lsp::TextEdit> DefaultLanguageServer::onTypeFormatting(
    const lsp::DocumentOnTypeFormattingParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  readDocument(params.textDocument.uri.toString(), _states.formatting, token,
               [&](const LspServices& services, const Document& d) {
                 if (const auto& formatter = services.formatter) {
                   if (formats_on(*formatter, params.ch))
                     edits = formatter->formatDocumentOnType(d, params, token);
                 }
               });
  return edits;
}

// The two hierarchies. A preparation is asked at a position, so it routes
// through the document the caret is in; the four expansions carry an item this
// server itself produced, and the URI inside it is what names their document.
std::vector<lsp::CallHierarchyItem> DefaultLanguageServer::prepareCallHierarchy(
    const lsp::CallHierarchyPrepareParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.callHierarchy, token,
                       &LspServices::callHierarchyProvider, &CallHierarchyProvider::prepareCallHierarchy,
                       params.position);
}

std::vector<lsp::CallHierarchyIncomingCall>
DefaultLanguageServer::incomingCalls(
    const lsp::CallHierarchyIncomingCallsParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.item.uri, _states.callHierarchy, token,
                       &LspServices::callHierarchyProvider, &CallHierarchyProvider::incomingCalls,
                       params.item);
}

std::vector<lsp::CallHierarchyOutgoingCall>
DefaultLanguageServer::outgoingCalls(
    const lsp::CallHierarchyOutgoingCallsParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.item.uri, _states.callHierarchy, token,
                       &LspServices::callHierarchyProvider, &CallHierarchyProvider::outgoingCalls,
                       params.item);
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::prepareTypeHierarchy(
    const lsp::TypeHierarchyPrepareParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.textDocument.uri, _states.typeHierarchy, token,
                       &LspServices::typeHierarchyProvider, &TypeHierarchyProvider::prepareTypeHierarchy,
                       params.position);
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::supertypes(
    const lsp::TypeHierarchySupertypesParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.item.uri, _states.typeHierarchy, token,
                       &LspServices::typeHierarchyProvider, &TypeHierarchyProvider::supertypes,
                       params.item);
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::subtypes(
    const lsp::TypeHierarchySubtypesParams& params,
    const CancellationToken& token) const {
  return routeDocument(params.item.uri, _states.typeHierarchy, token,
                       &LspServices::typeHierarchyProvider, &TypeHierarchyProvider::subtypes,
                       params.item);
}

// A query is about no document, so the state waited for is the workspace's:
// half an index would silently answer half a query.
std::vector<lsp::WorkspaceSymbol> DefaultLanguageServer::workspaceSymbol(
    const lsp::WorkspaceSymbolParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::WorkspaceSymbol> symbols;
  if (!awaitScan(token)) return symbols;
  (void)readAtWorkspaceState(_states.workspaceSymbol, token, [&] {
    const auto& provider = *_shared.workspaceSymbolProvider;
    symbols = provider.getSymbols(params.query, token);
    if (_eagerSymbolRanges) {
      for (auto& symbol : symbols) {
        throwIfCancelled(token);
        if (std::holds_alternative<lsp::LocationUriOnly>(symbol.location))
          symbol = provider.resolveSymbol(std::move(symbol), token);
      }
      std::erase_if(symbols, [](const auto& symbol) {
        return std::holds_alternative<lsp::LocationUriOnly>(symbol.location);
      });
    }
  });
  return symbols;
}

lsp::WorkspaceSymbol DefaultLanguageServer::workspaceSymbolResolve(
    const lsp::WorkspaceSymbol& symbol, const CancellationToken& token) const {
  lsp::WorkspaceSymbol resolved = symbol;
  if (!awaitScan(token)) return resolved;
  (void)readAtWorkspaceState(_states.workspaceSymbol, token, [&] {
    resolved = _shared.workspaceSymbolProvider->resolveSymbol(symbol, token);
  });
  return resolved;
}

// No wait and no lock: a command is the workspace's own code, and takes
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

// Mutate, then build, in one write the next update supersedes past its
// changes. The build always runs, then the first failure goes on. Room for
// `most` records comes before the mutation, so an installed text always
// reaches the build.
void DefaultLanguageServer::writeAndPublish(
    std::size_t most, cursiv::FunctionRef<void(TextChanges&)> mutate) {
  _shared.workspaceLock->write(
      [&](const CancellationToken& token) {
        TextChanges changes;
        std::vector<std::string_view> changed;
        std::vector<std::string_view> deleted;
        std::exception_ptr failure;
        try {
          changed.reserve(most);
          deleted.reserve(most);
          mutate(changes);
        } catch (...) {
          failure = std::current_exception();
        }
        // Past the room made, a copy that cannot grow leaves its records out.
        try {
          for (const auto& [uri, gone] : changes)
            (gone ? deleted : changed).push_back(uri);
        } catch (...) {
          if (failure == nullptr) failure = std::current_exception();
        }
        buildUpdate(changed, deleted, token);
        if (failure) std::rethrow_exception(failure);
      });
}

void DefaultLanguageServer::buildUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted, const CancellationToken& token) {
  try {
    _shared.documentBuilder->update(changed, deleted, token);
  } catch (const OperationCancelled&) {
    throw;
  } catch (const std::exception& failure) {
    if (!_buildFailureShown.exchange(true)) {
      // Named by its one document, else as the workspace. Showing it never
      // takes the failure's place.
      try {
        const std::string name =
            changed.size() + deleted.size() != 1 ? "the workspace"
            : changed.empty() ? uriFileName(deleted.front())
                              : uriFileName(changed.front());
        const char* const why = dynamic_cast<const std::bad_alloc*>(&failure)
                                    ? "out of memory"
                                    : failure.what();
        showError("cursiv: building " + name + " failed: " + why);
      } catch (...) {  // NOLINT(bugprone-empty-catch)
      }
    }
    throw;
  }
}

void DefaultLanguageServer::onBuildPhase(DocumentState reached,
                                         std::span<Document* const>,
                                         const CancellationToken&) {
  if (reached == DocumentState::Linked) {
    refreshAnnouncements();
  } else if (reached == DocumentState::Validated) {
    _buildFailureShown = false;
    publishPendingDiagnostics();
  }
}

void DefaultLanguageServer::onDocumentPhase(Document& document,
                                            DocumentState reached,
                                            const CancellationToken&) {
  if (reached == DocumentState::Validated) queueDiagnostics(document);
}

void DefaultLanguageServer::onDocumentBuildFailed(const Document& document) {
  queueDiagnostics(document);
}

void DefaultLanguageServer::queueDiagnostics(const Document& document) {
  const std::scoped_lock lock(_diagnosticsMutex);
  try {
    _pendingDiagnostics.push_back(document.id);
  } catch (...) {
    // A full publication retries any document whose record was lost.
    _announceAll = true;
  }
}

void DefaultLanguageServer::refreshAnnouncements() {
  for (auto it = _announced.begin(); it != _announced.end();) {
    const auto [id, failed] = *it++;
    const auto d = _shared.documents->getDocument(id);
    if (!d)
      publishRemoval(_shared.documents->getDocumentUri(id));
    else if (failed && !d->buildFailed && d->state >= DocumentState::Linked)
      queueDiagnostics(*d);
  }
}

// Publishes completed diagnostics once per document, including build failures.
void DefaultLanguageServer::publishPendingDiagnostics() {
  std::vector<DocumentId> pending;
  bool all = false;
  {
    const std::scoped_lock lock(_diagnosticsMutex);
    pending.swap(_pendingDiagnostics);
    all = std::exchange(_announceAll, false);
  }
  const auto announce = [this](const std::shared_ptr<Document>& d) {
    if (d &&
        (d->buildFailed || d->state.load() >= DocumentState::IndexedReferences))
      announceDocument(*d);
  };
  try {
    if (all) {
      for (const auto& d : _shared.documents->all()) announce(d);
      return;
    }
    std::ranges::sort(pending);
    const auto repeated = std::ranges::unique(pending);
    pending.erase(repeated.begin(), repeated.end());
    for (const DocumentId id : pending)
      announce(_shared.documents->getDocument(id));
  } catch (...) {
    const std::scoped_lock lock(_diagnosticsMutex);
    _announceAll = true;
    throw;
  }
}

// Recorded before publishing: a document the client may show is cleared once
// gone.
void DefaultLanguageServer::announceDocument(const Document& document) {
  _announced.insert_or_assign(document.id, document.buildFailed.load());
  publish(document);
}

void DefaultLanguageServer::publish(const Document& document) const {
  LanguageClient* client = _shared.languageClient.get();
  if (client == nullptr) return;

  lsp::PublishDiagnosticsParams params;
  params.uri = lsp::Uri::parse(document.uri);
  // The client's version is an `int` on the wire and the store's is unsigned;
  // the one narrowing the protocol imposes happens here and nowhere else.
  params.version = static_cast<int>(document.textDocument().version());

  params.diagnostics.reserve(document.diagnostics.size());
  for (const Diagnostic& diagnostic : document.diagnostics)
    params.diagnostics.push_back(
        detail::to_lsp_diagnostic(document, diagnostic, *_shared.documents));
  client->publishDiagnostics(std::move(params));
}

void DefaultLanguageServer::publishRemoval(std::string_view uri) {
  if (LanguageClient* client = _shared.languageClient.get()) {
    lsp::PublishDiagnosticsParams params;
    params.uri = lsp::Uri::parse(uri);
    client->publishDiagnostics(std::move(params));
  }
  // Forgotten once the empty snapshot is out, so a failed one is sent again. A
  // URI recreated later is announced anew.
  if (const DocumentId id = _shared.documents->getDocumentId(uri);
      id != kNoDocument)
    _announced.erase(id);
}

}  // namespace cursiv
