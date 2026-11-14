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

[[nodiscard]] bool lists_markdown(
    const std::optional<lsp::Array<lsp::MarkupKindEnum>>& formats) {
  return formats.has_value() &&
         std::ranges::any_of(*formats, [](const lsp::MarkupKindEnum& kind) {
           return kind == lsp::MarkupKind::Markdown;
         });
}

// Labels the providers' markdown as plain text, for a client that does not
// read markdown.
void as_plain_text(lsp::MarkupContent& content) {
  if (content.kind == lsp::MarkupKind::Markdown)
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::PlainText);
}

// A `didChange`'s changes in the core's units, strings copied: the params do
// not outlive the call.
std::vector<TextDocumentContentChangeEvent> from_lsp(
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
    converted.emplace_back(cursiv::from_lsp(ranged.range), ranged.text);
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
    _folders.emplace_back(uri, uri_file_name(uri));
  } else if (params.rootPath.has_value() && !params.rootPath->isNull() &&
             !params.rootPath->value().empty()) {
    const std::string uri = path_to_file_uri(params.rootPath->value());
    _folders.emplace_back(uri, uri_file_name(uri));
  }
  _scanPending = !_folders.empty();
  // Whether the client accepts the file watcher registered at `initialized`.
  _dynamicFileWatching =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles.has_value() &&
      params.capabilities.workspace->didChangeWatchedFiles->dynamicRegistration
          .value_or(false);
  // Versioned edits let the client refuse a quick fix that went stale after
  // it was built.
  _versionedEdits =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->workspaceEdit.has_value() &&
      params.capabilities.workspace->workspaceEdit->documentChanges.value_or(
          false);
  _markdownHover = false;
  _markdownCompletion = false;
  if (const auto& text = params.capabilities.textDocument) {
    if (text->hover.has_value())
      _markdownHover = lists_markdown(text->hover->contentFormat);
    if (text->completion.has_value() &&
        text->completion->completionItem.has_value())
      _markdownCompletion =
          lists_markdown(text->completion->completionItem->documentationFormat);
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
  const std::shared_ptr<Document> existing = documents.getDocument(uri);
  // The language is asked about the uri the document was bound with.
  const std::string normalized = normalize_uri(uri);
  const bool moved =
      existing == nullptr || existing->textDocument().getText() != item.text ||
      existing->language != _shared.serviceRegistry->findServices(
                                normalized, item.languageId.value());
  // Recorded before the text moves: a text installed is a text built.
  if (moved) {
    std::erase(changes.deleted, uri);
    changes.changed.push_back(uri);
  }
  if (existing == nullptr) {
    // Installed at `Changed`: the build parses the client's text, in
    // parallel with the rest and under its token, so the next keystroke
    // can cut it short.
    documents.addDocument(_shared.documentFactory->create(
        TextDocument::create(uri, item.languageId, item.version, item.text)));
  } else if (moved) {
    // The document stays the same object.
    _shared.documentFactory->attachTextDocument(
        *existing, TextDocument::create(uri, item.languageId,
                                        static_cast<std::int64_t>(item.version),
                                        item.text));
  }
  if (const std::shared_ptr<Document> open = documents.getDocument(uri)) {
    open->isOpen = true;
    // The client's number over the scan's zero, whether or not the bytes
    // moved: a diagnostic and a versioned edit report it back.
    if (!moved) updateVersion(*open, static_cast<std::int64_t>(item.version));
  }
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
  // A change on a URI nothing opened is not this server's to guess at.
  if (document == nullptr) return;
  if (version < document->textDocument().version()) return;
  // A range outside the text is clamped, not rejected -- the core's
  // `offsetAt` is total.
  if (!_shared.documentFactory->updateTextDocument(*document, from_lsp(edits),
                                                   version)) {
    updateVersion(*document, version);
    return;
  }
  changes.changed.push_back(std::move(uri));
}

void DefaultLanguageServer::updateVersion(Document& document,
                                          std::int64_t version) {
  if (document.textDocument().version() == version) return;
  _shared.documentFactory->attachVersion(document, version);
  // The bytes did not move, so a validated verdict stands; the client receives
  // it again under the version it now knows the document by. A document not
  // validated yet is published by the build that follows.
  if (document.state.load() == DocumentState::Validated)
    announceDocument(document);
}

// Closing a tab is not deleting a file: the saved content replaces the buffer
// and the document stays. Only a URI with nothing saved behind it, such as an
// `untitled:` buffer or a notebook cell, leaves.
void DefaultLanguageServer::closeText(std::string_view uri,
                                      TextChanges& changes) const {
  const std::shared_ptr<Document> closing = _shared.documents->getDocument(uri);
  if (closing == nullptr) return;
  if (const bool saved = has_saved_content(
          *_shared.fileSystemProvider,
          _shared.textDocumentContentProvider.get(), closing->uri);
      saved) {
    changes.changed.emplace_back(uri);
  } else {
    std::erase(changes.changed, uri);
    changes.deleted.emplace_back(uri);
  }
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
  deliverThenWrite(
      notebooks, [&] { notebooks->didChange(params); }, records,
      [&](TextChanges& changes) {
        if (!cells.has_value()) return;
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
    const NotebookDocuments* notebooks, cursiv::function_ref<void()> deliver,
    std::size_t most, cursiv::function_ref<void(TextChanges&)> mutate) {
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
          changes.deleted.push_back(std::move(uri));
          return;
        }
      }
      changes.changed.push_back(std::move(uri));
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

// The three `will` arms are read actions, like a command: the edit is computed
// off the workspace and applied by the client, so nothing here mutates.
std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willCreateFiles(
    const lsp::CreateFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  const FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willCreateFiles(params); }, token);
  return edit;
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willRenameFiles(
    const lsp::RenameFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  const FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willRenameFiles(params); }, token);
  return edit;
}

std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::willDeleteFiles(
    const lsp::DeleteFilesParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  const FileOperationHandler* files = _shared.fileOperationHandler.get();
  if (files == nullptr) return edit;
  (void)_shared.workspaceLock->read(
      [&]() { edit = files->willDeleteFiles(params); }, token);
  return edit;
}

// Rewrites `changes` as versioned `documentChanges` for a client that reads
// them, so a stale click is refused. An action's own `documentChanges` stay.
void DefaultLanguageServer::addDocumentVersion(lsp::CodeAction& action,
                                               const Document& document) {
  if (!action.edit.has_value() || !action.edit->changes.has_value() ||
      action.edit->documentChanges.has_value())
    return;
  const lsp::DocumentUri self = lsp::Uri::parse(document.uri);
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
  if (!required && !_scanPending.load()) return true;
  return _shared.workspaceManager->waitUntilReady(token);
}

// The lookup is a read of the store like any other -- a write rehashes the map
// it walks -- and it is over before the wait, which may not hold the readers'
// side.
bool DefaultLanguageServer::awaitState(std::string_view uri,
                                       DocumentState state,
                                       const CancellationToken& token) const {
  DocumentId id = kNoDocument;
  if (!_shared.workspaceLock->read(
          [&] { id = _shared.documents->getDocumentId(uri); }, token,
          WorkspaceLock::ReadPriority::Immediate))
    return false;
  return _shared.documentBuilder->waitUntil(state, id, token) ==
         WaitOutcome::Reached;
}

// A state that is cancelled, unreachable or lost to a failed build answers
// empty; the runtime turns a cancelled token into the protocol's refusal.
template <class Service, class Call>
void DefaultLanguageServer::routeDocument(
    std::string_view uri, ServiceRequirement requirement,
    std::unique_ptr<const Service> LspServices::* slot,
    const CancellationToken& token, Call call) const {
  const bool workspace =
      requirement.type == ServiceRequirement::Type::Workspace;
  if (!awaitScan(token)) return;
  // False when a later write reset the document after the wait: it is then
  // awaited again. A document below a workspace state the workspace reached was
  // left out of the build, which answers empty.
  const auto answer = [&]() {
    const std::shared_ptr<Document> document =
        _shared.documents->getDocument(uri);
    if (document == nullptr) return true;
    if (document->state.load() < requirement.state) return workspace;
    // The factory chose the language from both URI and languageId. It may
    // be core-only, in which case this document has no LSP provider.
    const auto* services = dynamic_cast<const LspServices*>(document->language);
    if (services == nullptr) return true;
    const Service* const provider = (services->*slot).get();
    if (provider == nullptr) return true;
    call(*provider, *document);
    return true;
  };
  if (workspace) {
    (void)readAtWorkspaceState(requirement.state, token,
                               [&] { (void)answer(); });
    return;
  }
  // Waits for the state an `Immediate` read needs, so the request reads beside
  // the rest of the build. The state is checked again inside the lock.
  for (bool answered = false; !answered;) {
    if (!awaitState(uri, requirement.state, token)) return;
    if (!_shared.workspaceLock->read([&] { answered = answer(); }, token,
                                     WorkspaceLock::ReadPriority::Immediate))
      return;
  }
}

// A workspace state is the builder's, not a document's. It is checked again
// inside the lock, where a write may have lowered it, and then awaited again.
bool DefaultLanguageServer::readAtWorkspaceState(
    DocumentState state, const CancellationToken& token,
    cursiv::function_ref<void()> read) const {
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

std::vector<lsp::DocumentSymbol> DefaultLanguageServer::documentSymbol(
    const lsp::DocumentSymbolParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentSymbol> symbols;
  routeDocument(params.textDocument.uri.toString(), _states.documentSymbol,
                &LspServices::documentSymbolProvider, token,
                [&](const DocumentSymbolProvider& provider, const Document& d) {
                  symbols = provider.getSymbols(d, token);
                });
  return symbols;
}

// The provider gets only the asking document: a target in another document,
// even one the client never opened, carries its own positions.
std::vector<lsp::LocationLink> DefaultLanguageServer::definition(
    const lsp::DefinitionParams& params, const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(), _states.definition,
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
  routeDocument(params.textDocument.uri.toString(), _states.declaration,
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
  routeDocument(params.textDocument.uri.toString(), _states.typeDefinition,
                &LspServices::typeDefinitionProvider, token,
                [&](const TypeDefinitionProvider& provider, const Document& d) {
                  if (std::optional<std::vector<lsp::LocationLink>> found =
                          provider.getTypeDefinition(d, params.position, token))
                    links = std::move(*found);
                });
  return links;
}

std::vector<lsp::LocationLink> DefaultLanguageServer::implementation(
    const lsp::ImplementationParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::LocationLink> links;
  routeDocument(params.textDocument.uri.toString(), _states.implementation,
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
  routeDocument(params.textDocument.uri.toString(), _states.references,
                &LspServices::referencesProvider, token,
                [&](const ReferencesProvider& provider, const Document& d) {
                  locations = provider.findReferences(
                      d, params.position, params.context.includeDeclaration,
                      token);
                });
  return locations;
}

std::optional<lsp::Range> DefaultLanguageServer::prepareRename(
    const lsp::PrepareRenameParams& params,
    const CancellationToken& token) const {
  std::optional<lsp::Range> span;
  routeDocument(params.textDocument.uri.toString(), _states.rename,
                &LspServices::renameProvider, token,
                [&](const RenameProvider& provider, const Document& d) {
                  span = provider.prepareRename(d, params.position, token);
                });
  return span;
}

// A rename is a read action: the client applies the edits and sends
// `didChange` back. So are the code action and formatting routes below.
std::optional<lsp::WorkspaceEdit> DefaultLanguageServer::rename(
    const lsp::RenameParams& params, const CancellationToken& token) const {
  std::optional<lsp::WorkspaceEdit> edit;
  routeDocument(params.textDocument.uri.toString(), _states.rename,
                &LspServices::renameProvider, token,
                [&](const RenameProvider& provider, const Document& d) {
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
      params.textDocument.uri.toString(), _states.documentHighlight,
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
  routeDocument(params.textDocument.uri.toString(), _states.semanticTokens,
                &LspServices::semanticTokenProvider, token,
                [&](const SemanticTokenProvider& provider, const Document& d) {
                  tokens = provider.semanticHighlight(d, params, token);
                });
  return tokens;
}

lsp::SemanticTokens DefaultLanguageServer::semanticTokensRange(
    const lsp::SemanticTokensRangeParams& params,
    const CancellationToken& token) const {
  lsp::SemanticTokens tokens;
  routeDocument(params.textDocument.uri.toString(), _states.semanticTokens,
                &LspServices::semanticTokenProvider, token,
                [&](const SemanticTokenProvider& provider, const Document& d) {
                  tokens = provider.semanticHighlightRange(d, params, token);
                });
  return tokens;
}

// A document nothing colours answers the empty tokens whole, as `full` does.
lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta>
DefaultLanguageServer::semanticTokensFullDelta(
    const lsp::SemanticTokensDeltaParams& params,
    const CancellationToken& token) const {
  lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta> tokens;
  routeDocument(params.textDocument.uri.toString(), _states.semanticTokens,
                &LspServices::semanticTokenProvider, token,
                [&](const SemanticTokenProvider& provider, const Document& d) {
                  tokens = provider.semanticHighlightDelta(d, params, token);
                });
  return tokens;
}

std::vector<lsp::InlayHint> DefaultLanguageServer::inlayHint(
    const lsp::InlayHintParams& params, const CancellationToken& token) const {
  std::vector<lsp::InlayHint> hints;
  routeDocument(params.textDocument.uri.toString(), _states.inlayHint,
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
  routeDocument(params.textDocument.uri.toString(), _states.foldingRange,
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
  routeDocument(params.textDocument.uri.toString(), _states.selectionRange,
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
  routeDocument(params.textDocument.uri.toString(), _states.hover,
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
  routeDocument(params.textDocument.uri.toString(), _states.completion,
                &LspServices::completionProvider, token,
                [&](const CompletionProvider& provider, const Document& d) {
                  list = provider.getCompletion(d, params, token);
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
  routeDocument(params.textDocument.uri.toString(), _states.signatureHelp,
                &LspServices::signatureHelpProvider, token,
                [&](const SignatureHelpProvider& provider, const Document& d) {
                  help = provider.provideSignatureHelp(d, params, token);
                });
  return help;
}

std::vector<lsp::CodeAction> DefaultLanguageServer::codeAction(
    const lsp::CodeActionParams& params, const CancellationToken& token) const {
  std::vector<lsp::CodeAction> actions;
  routeDocument(params.textDocument.uri.toString(), _states.codeAction,
                &LspServices::codeActionProvider, token,
                [&](const CodeActionProvider& provider, const Document& d) {
                  actions = provider.getCodeActions(d, params, token);
                  if (_versionedEdits)
                    for (lsp::CodeAction& action : actions)
                      addDocumentVersion(action, d);
                });
  return actions;
}

std::vector<lsp::DocumentLink> DefaultLanguageServer::documentLink(
    const lsp::DocumentLinkParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentLink> links;
  routeDocument(params.textDocument.uri.toString(), _states.documentLink,
                &LspServices::documentLinkProvider, token,
                [&](const DocumentLinkProvider& provider, const Document& d) {
                  links = provider.getDocumentLinks(d, token);
                });
  return links;
}

std::vector<lsp::CodeLens> DefaultLanguageServer::codeLens(
    const lsp::CodeLensParams& params, const CancellationToken& token) const {
  std::vector<lsp::CodeLens> lenses;
  routeDocument(params.textDocument.uri.toString(), _states.codeLens,
                &LspServices::codeLensProvider, token,
                [&](const CodeLensProvider& provider, const Document& d) {
                  lenses = provider.provideCodeLens(d, token);
                });
  return lenses;
}

// One slot and one state for the three arms, one call each.
std::vector<lsp::TextEdit> DefaultLanguageServer::formatting(
    const lsp::DocumentFormattingParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TextEdit> edits;
  routeDocument(params.textDocument.uri.toString(), _states.formatting,
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
  routeDocument(params.textDocument.uri.toString(), _states.formatting,
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
  routeDocument(params.textDocument.uri.toString(), _states.formatting,
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
  routeDocument(params.textDocument.uri.toString(), _states.callHierarchy,
                &LspServices::callHierarchyProvider, token,
                [&](const CallHierarchyProvider& provider, const Document& d) {
                  items =
                      provider.prepareCallHierarchy(d, params.position, token);
                });
  return items;
}

std::vector<lsp::CallHierarchyIncomingCall>
DefaultLanguageServer::incomingCalls(
    const lsp::CallHierarchyIncomingCallsParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::CallHierarchyIncomingCall> calls;
  routeDocument(params.item.uri.toString(), _states.callHierarchy,
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
  routeDocument(params.item.uri.toString(), _states.callHierarchy,
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
  routeDocument(params.textDocument.uri.toString(), _states.typeHierarchy,
                &LspServices::typeHierarchyProvider, token,
                [&](const TypeHierarchyProvider& provider, const Document& d) {
                  items =
                      provider.prepareTypeHierarchy(d, params.position, token);
                });
  return items;
}

std::vector<lsp::TypeHierarchyItem> DefaultLanguageServer::supertypes(
    const lsp::TypeHierarchySupertypesParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::TypeHierarchyItem> items;
  routeDocument(params.item.uri.toString(), _states.typeHierarchy,
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
  routeDocument(params.item.uri.toString(), _states.typeHierarchy,
                &LspServices::typeHierarchyProvider, token,
                [&](const TypeHierarchyProvider& provider, const Document&) {
                  items = provider.subtypes(params.item, token);
                });
  return items;
}

// A query is about no document, so the state waited for is the workspace's:
// half an index would silently answer half a query.
std::vector<lsp::WorkspaceSymbol> DefaultLanguageServer::workspaceSymbol(
    const lsp::WorkspaceSymbolParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::WorkspaceSymbol> symbols;
  if (!awaitScan(token)) return symbols;
  (void)readAtWorkspaceState(_states.workspaceSymbol, token, [&] {
    symbols = _shared.workspaceSymbolProvider->getSymbols(params.query, token);
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
    std::size_t most, cursiv::function_ref<void(TextChanges&)> mutate) {
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
          changed.assign(changes.changed.begin(), changes.changed.end());
          deleted.assign(changes.deleted.begin(), changes.deleted.end());
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
            : changed.empty() ? uri_file_name(deleted.front())
                              : uri_file_name(changed.front());
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
    clearRemoved();
  } else if (reached == DocumentState::Validated) {
    _buildFailureShown = false;
    publishValidated();
  }
}

void DefaultLanguageServer::onDocumentPhase(Document& document,
                                            DocumentState reached,
                                            const CancellationToken&) {
  if (reached != DocumentState::Validated) return;
  const std::scoped_lock lock(_validatedMutex);
  try {
    _validated.push_back(document.id);
  } catch (...) {
    // Not recorded, and not validated again by the next build: the next
    // drain announces every validated document instead.
    _announceAll = true;
  }
}

// Clears the documents the client was shown something for and which are gone.
// One still held keeps what the client shows until its next validation.
void DefaultLanguageServer::clearRemoved() {
  for (auto it = _announced.begin(); it != _announced.end();) {
    const DocumentId id = *it++;
    if (_shared.documents->getDocument(id) == nullptr)
      publishRemoval(_shared.documents->getDocumentUri(id));
  }
}

// Announces each document validated since the last drain, once, clean or not,
// unless gone or set back since. After a failure or a lost record, the next
// drain announces every validated document.
void DefaultLanguageServer::publishValidated() {
  std::vector<DocumentId> validated;
  bool all = false;
  {
    const std::scoped_lock lock(_validatedMutex);
    validated.swap(_validated);
    all = std::exchange(_announceAll, false);
  }
  try {
    if (all) {
      for (const std::shared_ptr<Document>& d : _shared.documents->all())
        if (d->state.load() == DocumentState::Validated) announceDocument(*d);
      return;
    }
    std::ranges::sort(validated);
    const auto repeated = std::ranges::unique(validated);
    validated.erase(repeated.begin(), repeated.end());
    for (const DocumentId id : validated) {
      const std::shared_ptr<Document> d = _shared.documents->getDocument(id);
      if (d != nullptr && d->state.load() == DocumentState::Validated)
        announceDocument(*d);
    }
  } catch (...) {
    const std::scoped_lock lock(_validatedMutex);
    _announceAll = true;
    throw;
  }
}

// Recorded before publishing: a document the client may show is cleared once
// gone.
void DefaultLanguageServer::announceDocument(const Document& document) {
  _announced.insert(document.id);
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
      // integer to `int` here, the one place that handles both shapes.
      if (d.code.has_value()) {
        if (d.code->isString())
          published.code = d.code->string();
        else
          published.code = static_cast<int>(d.code->integer());
      }
      // `data` comes back untouched in `codeAction` requests: it carries the
      // quick fixes.
      if (d.data.has_value()) published.data = detail::to_lsp_any(*d.data);
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
      // Each row names its own document (an empty `uri` means this one), whose
      // index places it. An unheld document answers nothing.
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
