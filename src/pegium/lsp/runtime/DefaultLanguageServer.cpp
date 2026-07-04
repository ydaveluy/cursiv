#include <pegium/lsp/runtime/DefaultLanguageServer.hpp>

#include <functional>
#include <future>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <lsp/messages.h>

#include <pegium/core/utils/Disposable.hpp>
#include <pegium/core/utils/Errors.hpp>
#include <pegium/lsp/runtime/internal/RuntimeObservability.hpp>
#include <pegium/lsp/services/LanguageServerFeatures.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/lsp/workspace/WorkspaceAdapters.hpp>

namespace pegium {

namespace {

::lsp::Array<::lsp::String> to_lsp_array(
    const std::set<std::string, std::less<>> &values) {
  ::lsp::Array<::lsp::String> array;
  array.reserve(values.size());
  for (const auto &value : values) {
    array.emplace_back(value);
  }
  return array;
}

void merge_indexed_strings(std::vector<std::string> &merged,
                           const ::lsp::Array<::lsp::String> &incoming,
                           std::string_view label) {
  if (merged.size() < incoming.size()) {
    merged.resize(incoming.size());
  }
  for (std::size_t index = 0; index < incoming.size(); ++index) {
    const auto &value = incoming[index];
    if (merged[index].empty()) {
      merged[index] = value;
    } else if (merged[index] != value) {
      throw utils::LanguageServerError(
          "Cannot merge '" + merged[index] + "' and '" + std::string(value) +
          "' " + std::string(label) + ". They use the same index " +
          std::to_string(index) + ".");
    }
  }
}

struct LanguageServerCapabilities {
  std::optional<::lsp::CompletionOptions> completionOptions;
  std::optional<::lsp::SignatureHelpOptions> signatureHelpOptions;
  std::optional<::lsp::DocumentOnTypeFormattingOptions>
      formattingOnTypeOptions;
  std::optional<::lsp::SemanticTokensOptions> semanticTokensOptions;
  bool hasHoverProvider = false;
  bool hasDeclarationProvider = false;
  bool hasDefinitionProvider = false;
  bool hasTypeDefinitionProvider = false;
  bool hasImplementationProvider = false;
  bool hasReferencesProvider = false;
  bool hasDocumentHighlightProvider = false;
  bool hasDocumentSymbolProvider = false;
  bool hasCodeActionProvider = false;
  bool hasCodeLensProvider = false;
  bool hasCodeLensResolveProvider = false;
  bool hasDocumentLinkProvider = false;
  bool hasFormattingProvider = false;
  bool hasFoldingRangeProvider = false;
  bool hasSelectionRangeProvider = false;
  bool hasCallHierarchyProvider = false;
  bool hasTypeHierarchyProvider = false;
  bool hasInlayHintProvider = false;
  bool hasRenameProvider = false;
};

LanguageServerCapabilities collect_language_server_capabilities(
    const SharedServices &sharedServices) {
  LanguageServerCapabilities capabilities;
  std::set<std::string, std::less<>> completionTriggerCharacters;
  std::set<std::string, std::less<>> allCommitCharacters;
  std::set<std::string, std::less<>> signatureTriggerCharacters;
  std::set<std::string, std::less<>> signatureRetriggerCharacters;
  bool hasSignatureHelpProvider = false;
  std::vector<std::string> semanticTokenTypes;
  std::vector<std::string> semanticTokenModifiers;
  bool hasSemanticTokenProvider = false;
  bool semanticTokensFull = true;
  bool semanticTokensDelta = true;
  bool semanticTokensRange = true;

  for (const auto *coreServices : sharedServices.serviceRegistry->all()) {
    const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
    if (services == nullptr) {
      continue;
    }

    if (const auto *provider = services->lsp.completionProvider.get();
        provider != nullptr) {
      if (!capabilities.completionOptions.has_value()) {
        capabilities.completionOptions.emplace();
      }
      if (const auto options = provider->completionOptions();
          options.has_value()) {
        for (const auto &character : options->triggerCharacters) {
          completionTriggerCharacters.emplace(character);
        }
        for (const auto &character : options->allCommitCharacters) {
          allCommitCharacters.emplace(character);
        }
      }
    }

    if (const auto *provider = services->lsp.signatureHelp.get();
        provider != nullptr) {
      hasSignatureHelpProvider = true;
      const auto options = provider->signatureHelpOptions();
      if (options.triggerCharacters.has_value()) {
        for (const auto &character : *options.triggerCharacters) {
          signatureTriggerCharacters.emplace(character);
        }
      }
      if (options.retriggerCharacters.has_value()) {
        for (const auto &character : *options.retriggerCharacters) {
          signatureRetriggerCharacters.emplace(character);
        }
      }
    }

    if (!capabilities.formattingOnTypeOptions.has_value() &&
        services->lsp.formatter != nullptr) {
      capabilities.formattingOnTypeOptions =
          services->lsp.formatter->formatOnTypeOptions();
    }

    if (const auto *provider = services->lsp.semanticTokenProvider.get();
        provider != nullptr) {
      hasSemanticTokenProvider = true;
      const auto options = provider->semanticTokensOptions();
      merge_indexed_strings(semanticTokenTypes, options.legend.tokenTypes,
                            "token types");
      merge_indexed_strings(semanticTokenModifiers,
                            options.legend.tokenModifiers, "token modifier");

      if (!options.full.has_value()) {
        semanticTokensFull = false;
      } else if (const auto *asBool = std::get_if<bool>(&*options.full)) {
        semanticTokensFull = semanticTokensFull && *asBool;
        // A provider that reports `full` via a plain bool gives no delta
        // guarantee, so narrow the merged delta capability conservatively.
        semanticTokensDelta = false;
      } else if (const auto *asOptions =
                     std::get_if<::lsp::SemanticTokensOptionsFull>(
                         &*options.full)) {
        const auto supportsDelta =
            asOptions->delta.has_value() ? *asOptions->delta : false;
        semanticTokensDelta = semanticTokensDelta && supportsDelta;
      }

      if (!options.range.has_value()) {
        semanticTokensRange = false;
      } else if (const auto *asBool = std::get_if<bool>(&*options.range)) {
        semanticTokensRange = semanticTokensRange && *asBool;
      }
    }

    capabilities.hasHoverProvider =
        capabilities.hasHoverProvider ||
        services->lsp.hoverProvider != nullptr;
    capabilities.hasDeclarationProvider =
        capabilities.hasDeclarationProvider ||
        services->lsp.declarationProvider != nullptr;
    capabilities.hasDefinitionProvider =
        capabilities.hasDefinitionProvider ||
        services->lsp.definitionProvider != nullptr;
    capabilities.hasTypeDefinitionProvider =
        capabilities.hasTypeDefinitionProvider ||
        services->lsp.typeProvider != nullptr;
    capabilities.hasImplementationProvider =
        capabilities.hasImplementationProvider ||
        services->lsp.implementationProvider != nullptr;
    capabilities.hasReferencesProvider =
        capabilities.hasReferencesProvider ||
        services->lsp.referencesProvider != nullptr;
    capabilities.hasDocumentHighlightProvider =
        capabilities.hasDocumentHighlightProvider ||
        services->lsp.documentHighlightProvider != nullptr;
    capabilities.hasDocumentSymbolProvider =
        capabilities.hasDocumentSymbolProvider ||
        services->lsp.documentSymbolProvider != nullptr;
    capabilities.hasCodeActionProvider =
        capabilities.hasCodeActionProvider ||
        services->lsp.codeActionProvider != nullptr;
    if (const auto *provider = services->lsp.codeLensProvider.get();
        provider != nullptr) {
      capabilities.hasCodeLensProvider = true;
      capabilities.hasCodeLensResolveProvider =
          capabilities.hasCodeLensResolveProvider ||
          provider->supportsResolveCodeLens();
    }
    capabilities.hasDocumentLinkProvider =
        capabilities.hasDocumentLinkProvider ||
        services->lsp.documentLinkProvider != nullptr;
    capabilities.hasFormattingProvider =
        capabilities.hasFormattingProvider || services->lsp.formatter != nullptr;
    capabilities.hasFoldingRangeProvider =
        capabilities.hasFoldingRangeProvider ||
        services->lsp.foldingRangeProvider != nullptr;
    capabilities.hasSelectionRangeProvider =
        capabilities.hasSelectionRangeProvider ||
        services->lsp.selectionRangeProvider != nullptr;
    capabilities.hasCallHierarchyProvider =
        capabilities.hasCallHierarchyProvider ||
        services->lsp.callHierarchyProvider != nullptr;
    capabilities.hasTypeHierarchyProvider =
        capabilities.hasTypeHierarchyProvider ||
        services->lsp.typeHierarchyProvider != nullptr;
    capabilities.hasInlayHintProvider =
        capabilities.hasInlayHintProvider ||
        services->lsp.inlayHintProvider != nullptr;
    capabilities.hasRenameProvider =
        capabilities.hasRenameProvider || services->lsp.renameProvider != nullptr;
  }

  if (capabilities.completionOptions.has_value()) {
    if (!completionTriggerCharacters.empty()) {
      capabilities.completionOptions->triggerCharacters =
          to_lsp_array(completionTriggerCharacters);
    }
    if (!allCommitCharacters.empty()) {
      capabilities.completionOptions->allCommitCharacters =
          to_lsp_array(allCommitCharacters);
    }
  }

  if (hasSignatureHelpProvider) {
    ::lsp::SignatureHelpOptions options{};
    if (!signatureTriggerCharacters.empty()) {
      options.triggerCharacters = to_lsp_array(signatureTriggerCharacters);
    }
    if (!signatureRetriggerCharacters.empty()) {
      options.retriggerCharacters = to_lsp_array(signatureRetriggerCharacters);
    }
    capabilities.signatureHelpOptions = std::move(options);
  }

  if (hasSemanticTokenProvider) {
    ::lsp::SemanticTokensOptions options{};
    for (const auto &tokenType : semanticTokenTypes) {
      if (!tokenType.empty()) {
        options.legend.tokenTypes.push_back(tokenType);
      }
    }
    for (const auto &tokenModifier : semanticTokenModifiers) {
      if (!tokenModifier.empty()) {
        options.legend.tokenModifiers.push_back(tokenModifier);
      }
    }
    options.range = semanticTokensRange;
    if (semanticTokensFull) {
      ::lsp::SemanticTokensOptionsFull fullOptions{};
      fullOptions.delta = semanticTokensDelta;
      options.full = fullOptions;
    } else {
      options.full = false;
    }
    capabilities.semanticTokensOptions = std::move(options);
  }

  return capabilities;
}

} // namespace

::lsp::InitializeResult
DefaultLanguageServer::initialize(const ::lsp::InitializeParams &params) {
  const auto initializeParams = to_workspace_initialize_params(params);
  shared.workspace.configurationProvider->initialize(initializeParams);
  shared.workspace.workspaceManager->initialize(initializeParams);

  if (!_initializeEventFired) {
    _onInitialize.emit(params);
    _onInitialize = {};
    _initializeEventFired = true;
  }

  ::lsp::InitializeResult result{};
  ::lsp::TextDocumentSyncOptions textSync{};
  textSync.openClose = true;
  textSync.change = ::lsp::TextDocumentSyncKind::Incremental;
  assert(shared.lsp.documentUpdateHandler != nullptr);
  textSync.save = shared.lsp.documentUpdateHandler->supportsDidSaveDocument();
  textSync.willSave =
      shared.lsp.documentUpdateHandler->supportsWillSaveDocument();
  textSync.willSaveWaitUntil = shared.lsp.documentUpdateHandler
                                   ->supportsWillSaveDocumentWaitUntil();

  const auto capabilities = collect_language_server_capabilities(shared);
  const bool hasWorkspaceSymbols = shared.lsp.workspaceSymbolProvider != nullptr;
  const auto executeCommands = pegium::getExecuteCommands(shared);

  result.capabilities.positionEncoding = ::lsp::PositionEncodingKind::UTF16;
  result.capabilities.textDocumentSync = textSync;
  if (capabilities.completionOptions.has_value()) {
    result.capabilities.completionProvider = *capabilities.completionOptions;
  }
  if (capabilities.hasHoverProvider) {
    result.capabilities.hoverProvider = true;
  }
  if (capabilities.signatureHelpOptions.has_value()) {
    result.capabilities.signatureHelpProvider =
        *capabilities.signatureHelpOptions;
  }
  if (capabilities.hasDeclarationProvider) {
    result.capabilities.declarationProvider = true;
  }
  if (capabilities.hasDefinitionProvider) {
    result.capabilities.definitionProvider = true;
  }
  if (capabilities.hasTypeDefinitionProvider) {
    result.capabilities.typeDefinitionProvider = true;
  }
  if (capabilities.hasImplementationProvider) {
    result.capabilities.implementationProvider = true;
  }
  if (capabilities.hasReferencesProvider) {
    result.capabilities.referencesProvider = true;
  }
  if (capabilities.hasDocumentHighlightProvider) {
    result.capabilities.documentHighlightProvider = true;
  }
  if (capabilities.hasDocumentSymbolProvider) {
    result.capabilities.documentSymbolProvider = true;
  }
  if (capabilities.hasCodeActionProvider) {
    result.capabilities.codeActionProvider = true;
  }
  if (capabilities.hasCodeLensProvider) {
    ::lsp::CodeLensOptions options{};
    options.resolveProvider = capabilities.hasCodeLensResolveProvider;
    result.capabilities.codeLensProvider = std::move(options);
  }
  if (capabilities.hasFoldingRangeProvider) {
    result.capabilities.foldingRangeProvider = true;
  }
  if (capabilities.hasDocumentLinkProvider) {
    ::lsp::DocumentLinkOptions options{};
    options.resolveProvider = false;
    result.capabilities.documentLinkProvider = std::move(options);
  }
  if (hasWorkspaceSymbols) {
    ::lsp::WorkspaceSymbolOptions options{};
    options.resolveProvider =
        shared.lsp.workspaceSymbolProvider->supportsResolveSymbol();
    result.capabilities.workspaceSymbolProvider = std::move(options);
  }
  if (capabilities.hasFormattingProvider) {
    result.capabilities.documentFormattingProvider = true;
    result.capabilities.documentRangeFormattingProvider = true;
  }
  if (capabilities.formattingOnTypeOptions.has_value()) {
    result.capabilities.documentOnTypeFormattingProvider =
        *capabilities.formattingOnTypeOptions;
  }
  if (capabilities.hasSelectionRangeProvider) {
    result.capabilities.selectionRangeProvider = true;
  }
  if (capabilities.hasRenameProvider) {
    ::lsp::RenameOptions renameOptions{};
    renameOptions.prepareProvider = true;
    result.capabilities.renameProvider = std::move(renameOptions);
  }
  if (!executeCommands.empty()) {
    ::lsp::ExecuteCommandOptions options{};
    options.commands = executeCommands;
    result.capabilities.executeCommandProvider = std::move(options);
  }
  if (capabilities.hasCallHierarchyProvider) {
    result.capabilities.callHierarchyProvider = ::lsp::CallHierarchyOptions{};
  }
  if (capabilities.hasTypeHierarchyProvider) {
    result.capabilities.typeHierarchyProvider = ::lsp::TypeHierarchyOptions{};
  }
  if (capabilities.hasInlayHintProvider) {
    ::lsp::InlayHintOptions options{};
    options.resolveProvider = false;
    result.capabilities.inlayHintProvider = std::move(options);
  }
  if (capabilities.semanticTokensOptions.has_value()) {
    result.capabilities.semanticTokensProvider =
        *capabilities.semanticTokensOptions;
  }

  ::lsp::ServerCapabilitiesWorkspace workspaceCapabilities{};
  ::lsp::WorkspaceFoldersServerCapabilities workspaceFolders{};
  workspaceFolders.supported = true;
  workspaceCapabilities.workspaceFolders = std::move(workspaceFolders);
  if (shared.lsp.fileOperationHandler != nullptr) {
    const auto options = shared.lsp.fileOperationHandler->fileOperationOptions();
    const bool hasFileOperations = options.didCreate.has_value() ||
                                   options.willCreate.has_value() ||
                                   options.didRename.has_value() ||
                                   options.willRename.has_value() ||
                                   options.didDelete.has_value() ||
                                   options.willDelete.has_value();
    if (hasFileOperations) {
      workspaceCapabilities.fileOperations = options;
    }
  }
  result.capabilities.workspace = std::move(workspaceCapabilities);

  ::lsp::InitializeResultServerInfo info{};
  info.name = "pegium";
  result.serverInfo = std::move(info);

  return result;
}

void DefaultLanguageServer::initialized(const ::lsp::InitializedParams &params) {
  const auto initializedParams =
      make_workspace_initialized_params(shared.lsp.languageClient.get());
  observe_background_task(
      shared, "ConfigurationProvider.initialized",
      shared.workspace.configurationProvider->initialized(initializedParams));
  observe_background_task(
      shared, "WorkspaceManager.initialized",
      shared.workspace.workspaceManager->initialized(initializedParams));
  if (!_initializedEventFired) {
    _onInitialized.emit(params);
    _onInitialized = {};
    _initializedEventFired = true;
  }
}

utils::ScopedDisposable DefaultLanguageServer::onInitialize(
    std::function<void(const ::lsp::InitializeParams &)> callback) {
  if (_initializeEventFired) {
    return {};
  }
  return _onInitialize.on(std::move(callback));
}

utils::ScopedDisposable DefaultLanguageServer::onInitialized(
    std::function<void(const ::lsp::InitializedParams &)> callback) {
  if (_initializedEventFired) {
    return {};
  }
  return _onInitialized.on(std::move(callback));
}

} // namespace pegium
