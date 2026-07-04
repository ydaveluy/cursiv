#include <pegium/lsp/runtime/LanguageServerRequestHandlerParts.hpp>

#include <algorithm>

#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/lsp/services/LanguageServerFeatures.hpp>
#include <pegium/lsp/runtime/LanguageServerRequestHandlerUtils.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace pegium {

namespace {

bool has_code_lens_resolve_provider(const pegium::SharedServices &sharedServices) {
  return std::ranges::any_of(
      sharedServices.serviceRegistry->all(), [](const auto *coreServices) {
        const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
        return services != nullptr && services->lsp.codeLensProvider != nullptr &&
               services->lsp.codeLensProvider->supportsResolveCodeLens();
      });
}

} // namespace

void addLanguageServerTextDocumentHandlers(
    LanguageServerHandlerContext &server, ::lsp::MessageHandler &handler,
    pegium::SharedServices &sharedServices,
    const ServiceRequirements &serviceRequirements) {
  const auto completionRequirement =
      serviceRequirements.CompletionProvider.value_or(
          workspace::DocumentState::Linked);
  const auto signatureHelpRequirement =
      serviceRequirements.SignatureHelp.value_or(
      workspace::DocumentState::IndexedReferences);
  const auto hoverRequirement = serviceRequirements.HoverProvider.value_or(
      workspace::DocumentState::Linked);
  const auto documentSymbolRequirement =
      serviceRequirements.DocumentSymbolProvider.value_or(
      workspace::DocumentState::Parsed);
  const auto codeLensRequirement = serviceRequirements.CodeLensProvider.value_or(
      workspace::DocumentState::IndexedReferences);
  const auto documentLinkRequirement =
      serviceRequirements.DocumentLinkProvider.value_or(
      workspace::DocumentState::Parsed);
  const auto formatterRequirement = serviceRequirements.Formatter.value_or(
      workspace::DocumentState::Parsed);
  const auto inlayHintRequirement =
      serviceRequirements.InlayHintProvider.value_or(
      workspace::DocumentState::IndexedReferences);
  const auto semanticTokenRequirement =
      serviceRequirements.SemanticTokenProvider.value_or(
      workspace::DocumentState::Linked);
  const auto callHierarchyRequirement =
      serviceRequirements.CallHierarchyProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto typeHierarchyRequirement =
      serviceRequirements.TypeHierarchyProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto foldingRangeRequirement =
      serviceRequirements.FoldingRangeProvider.value_or(
      workspace::DocumentState::Parsed);
  const auto declarationRequirement =
      serviceRequirements.DeclarationProvider.value_or(
      workspace::DocumentState::Linked);
  const auto definitionRequirement =
      serviceRequirements.DefinitionProvider.value_or(
      workspace::DocumentState::Linked);
  const auto typeDefinitionRequirement = serviceRequirements.TypeProvider.value_or(
      workspace::DocumentState::Linked);
  const auto implementationRequirement =
      serviceRequirements.ImplementationProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto referencesRequirement =
      serviceRequirements.ReferencesProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto documentHighlightRequirement =
      serviceRequirements.DocumentHighlightProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto renameRequirement =
      serviceRequirements.RenameProvider.value_or(
      WorkspaceState::IndexedReferences);
  const auto codeActionRequirement =
      serviceRequirements.CodeActionProvider.value_or(
      workspace::DocumentState::Validated);
  const auto selectionRangeRequirement =
      serviceRequirements.SelectionRangeProvider.value_or(
      workspace::DocumentState::Parsed);

  add_request_handler<::lsp::requests::TextDocument_Completion>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Completion>(
          server, sharedServices, completionRequirement,
          [&sharedServices](const ::lsp::CompletionParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getCompletion(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_CompletionResult>{}));

  add_request_handler<::lsp::requests::TextDocument_SignatureHelp>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_SignatureHelp>(
          server, sharedServices, signatureHelpRequirement,
          [&sharedServices](const ::lsp::SignatureHelpParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getSignatureHelp(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_SignatureHelpResult>{}));

  add_request_handler<::lsp::requests::TextDocument_Hover>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Hover>(
          server, sharedServices, hoverRequirement,
          [&sharedServices](const ::lsp::HoverParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getHoverContent(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_HoverResult>{}));

  add_request_handler<::lsp::requests::TextDocument_DocumentSymbol>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_DocumentSymbol>(
          server, sharedServices, documentSymbolRequirement,
          [&sharedServices](const ::lsp::DocumentSymbolParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getDocumentSymbols(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_DocumentSymbolResult>{}));

  add_request_handler<::lsp::requests::TextDocument_CodeLens>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_CodeLens>(
          server, sharedServices, codeLensRequirement,
          [&sharedServices](const ::lsp::CodeLensParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getCodeLens(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_CodeLensResult>{}));

  if (has_code_lens_resolve_provider(sharedServices)) {
    add_request_handler<::lsp::requests::CodeLens_Resolve>(
        handler,
        make_async_request<::lsp::requests::CodeLens_Resolve>(
            server,
            [&server, &sharedServices](::lsp::CodeLens &&codeLens,
                                       const utils::CancellationToken &cancelToken) {
              ensure_initialized(server);
              auto codeLensForResolve = codeLens;
              auto resolvedCodeLens =
                  resolveCodeLens(sharedServices, codeLensForResolve, cancelToken);
              return adapt_result<::lsp::CodeLens>(
                  server, std::move(resolvedCodeLens),
                  wrap_resolved_or_original<::lsp::CodeLens>{
                      std::move(codeLens)},
                  cancelToken);
            }));
  }

  add_request_handler<::lsp::requests::TextDocument_DocumentLink>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_DocumentLink>(
          server, sharedServices, documentLinkRequirement,
          [&sharedServices](const ::lsp::DocumentLinkParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getDocumentLinks(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_DocumentLinkResult>{}));

  add_request_handler<::lsp::requests::TextDocument_SelectionRange>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_SelectionRange>(
          server, sharedServices, selectionRangeRequirement,
          [&sharedServices](const ::lsp::SelectionRangeParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getSelectionRanges(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_SelectionRangeResult>{}));

  add_request_handler<::lsp::requests::TextDocument_Formatting>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Formatting>(
          server, sharedServices, formatterRequirement,
          [&sharedServices](const ::lsp::DocumentFormattingParams &params,
                            const utils::CancellationToken &cancelToken) {
            return formatDocument(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_FormattingResult>{}));

  add_request_handler<::lsp::requests::TextDocument_RangeFormatting>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_RangeFormatting>(
          server, sharedServices, formatterRequirement,
          [&sharedServices](
              const ::lsp::DocumentRangeFormattingParams &params,
              const utils::CancellationToken &cancelToken) {
            return formatDocumentRange(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_RangeFormattingResult>{}));

  add_request_handler<::lsp::requests::TextDocument_OnTypeFormatting>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_OnTypeFormatting>(
          server, sharedServices, formatterRequirement,
          [&sharedServices](
              const ::lsp::DocumentOnTypeFormattingParams &params,
              const utils::CancellationToken &cancelToken) {
            return formatDocumentOnType(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_OnTypeFormattingResult>{}));

  add_request_handler<::lsp::requests::TextDocument_InlayHint>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_InlayHint>(
          server, sharedServices, inlayHintRequirement,
          [&sharedServices](const ::lsp::InlayHintParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getInlayHints(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_InlayHintResult>{}));

  add_request_handler<::lsp::requests::TextDocument_SemanticTokens_Full>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_SemanticTokens_Full>(
          server, sharedServices, semanticTokenRequirement,
          [&sharedServices](const ::lsp::SemanticTokensParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getSemanticTokensFull(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_SemanticTokens_FullResult>{}));

  add_request_handler<::lsp::requests::TextDocument_SemanticTokens_Full_Delta>(
      handler,
      create_request_handler<
          ::lsp::requests::TextDocument_SemanticTokens_Full_Delta>(
          server, sharedServices, semanticTokenRequirement,
          [&sharedServices](const ::lsp::SemanticTokensDeltaParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getSemanticTokensDelta(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<
              ::lsp::TextDocument_SemanticTokens_Full_DeltaResult>{}));

  add_request_handler<::lsp::requests::TextDocument_SemanticTokens_Range>(
      handler,
      create_request_handler<
          ::lsp::requests::TextDocument_SemanticTokens_Range>(
          server, sharedServices, semanticTokenRequirement,
          [&sharedServices](const ::lsp::SemanticTokensRangeParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getSemanticTokensRange(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<
              ::lsp::TextDocument_SemanticTokens_RangeResult>{}));

  add_request_handler<::lsp::requests::TextDocument_PrepareCallHierarchy>(
      handler,
      create_request_handler<
          ::lsp::requests::TextDocument_PrepareCallHierarchy>(
          server, sharedServices, callHierarchyRequirement,
          [&sharedServices](const ::lsp::CallHierarchyPrepareParams &params,
                            const utils::CancellationToken &cancelToken) {
            return prepareCallHierarchy(sharedServices, params, cancelToken);
          },
          wrap_empty_vector_as_null<
              ::lsp::TextDocument_PrepareCallHierarchyResult>{}));

  add_request_handler<::lsp::requests::CallHierarchy_IncomingCalls>(
      handler,
      create_item_request_handler<::lsp::requests::CallHierarchy_IncomingCalls>(
          server, sharedServices, callHierarchyRequirement,
          [&sharedServices](
              const ::lsp::CallHierarchyIncomingCallsParams &params,
              const utils::CancellationToken &cancelToken) {
            return getIncomingCalls(sharedServices, params, cancelToken);
          },
          wrap_empty_vector_as_null<::lsp::CallHierarchy_IncomingCallsResult>{}));

  add_request_handler<::lsp::requests::CallHierarchy_OutgoingCalls>(
      handler,
      create_item_request_handler<::lsp::requests::CallHierarchy_OutgoingCalls>(
          server, sharedServices, callHierarchyRequirement,
          [&sharedServices](
              const ::lsp::CallHierarchyOutgoingCallsParams &params,
              const utils::CancellationToken &cancelToken) {
            return getOutgoingCalls(sharedServices, params, cancelToken);
          },
          wrap_empty_vector_as_null<::lsp::CallHierarchy_OutgoingCallsResult>{}));

  add_request_handler<::lsp::requests::TextDocument_PrepareTypeHierarchy>(
      handler,
      create_request_handler<
          ::lsp::requests::TextDocument_PrepareTypeHierarchy>(
          server, sharedServices, typeHierarchyRequirement,
          [&sharedServices](const ::lsp::TypeHierarchyPrepareParams &params,
                            const utils::CancellationToken &cancelToken) {
            return prepareTypeHierarchy(sharedServices, params, cancelToken);
          },
          wrap_empty_vector_as_null<
              ::lsp::TextDocument_PrepareTypeHierarchyResult>{}));

  add_request_handler<::lsp::requests::TypeHierarchy_Supertypes>(
      handler,
      create_item_request_handler<::lsp::requests::TypeHierarchy_Supertypes>(
          server, sharedServices, typeHierarchyRequirement,
          [&sharedServices](const ::lsp::TypeHierarchySupertypesParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getTypeHierarchySupertypes(sharedServices, params,
                                              cancelToken);
          },
          wrap_empty_vector_as_null<::lsp::TypeHierarchy_SupertypesResult>{}));

  add_request_handler<::lsp::requests::TypeHierarchy_Subtypes>(
      handler,
      create_item_request_handler<::lsp::requests::TypeHierarchy_Subtypes>(
          server, sharedServices, typeHierarchyRequirement,
          [&sharedServices](const ::lsp::TypeHierarchySubtypesParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getTypeHierarchySubtypes(sharedServices, params,
                                            cancelToken);
          },
          wrap_empty_vector_as_null<::lsp::TypeHierarchy_SubtypesResult>{}));

  add_request_handler<::lsp::requests::TextDocument_FoldingRange>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_FoldingRange>(
          server, sharedServices, foldingRangeRequirement,
          [&sharedServices](const ::lsp::FoldingRangeParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getFoldingRanges(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_FoldingRangeResult>{}));

  add_request_handler<::lsp::requests::TextDocument_Declaration>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Declaration>(
          server, sharedServices, declarationRequirement,
          [&sharedServices](const ::lsp::DeclarationParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getDeclaration(sharedServices, params, cancelToken);
          },
          wrap_optional_links<::lsp::TextDocument_DeclarationResult,
                              ::lsp::Declaration>{
              GotoLinkKind::Declaration}));

  add_request_handler<::lsp::requests::TextDocument_Definition>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Definition>(
          server, sharedServices, definitionRequirement,
          [&sharedServices](const ::lsp::DefinitionParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getDefinition(sharedServices, params, cancelToken);
          },
          wrap_optional_links<::lsp::TextDocument_DefinitionResult,
                              ::lsp::Definition>{
              GotoLinkKind::Definition}));

  add_request_handler<::lsp::requests::TextDocument_TypeDefinition>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_TypeDefinition>(
          server, sharedServices, typeDefinitionRequirement,
          [&sharedServices](const ::lsp::TypeDefinitionParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getTypeDefinition(sharedServices, params, cancelToken);
          },
          wrap_optional_links<::lsp::TextDocument_TypeDefinitionResult,
                              ::lsp::Definition>{
              GotoLinkKind::TypeDefinition}));

  add_request_handler<::lsp::requests::TextDocument_Implementation>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Implementation>(
          server, sharedServices, implementationRequirement,
          [&sharedServices](const ::lsp::ImplementationParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getImplementation(sharedServices, params, cancelToken);
          },
          wrap_optional_links<::lsp::TextDocument_ImplementationResult,
                              ::lsp::Definition>{
              GotoLinkKind::Implementation}));

  add_request_handler<::lsp::requests::TextDocument_References>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_References>(
          server, sharedServices, referencesRequirement,
          [&sharedServices](const ::lsp::ReferenceParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getReferences(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_ReferencesResult>{}));

  add_request_handler<::lsp::requests::TextDocument_DocumentHighlight>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_DocumentHighlight>(
          server, sharedServices, documentHighlightRequirement,
          [&sharedServices](const ::lsp::DocumentHighlightParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getDocumentHighlights(sharedServices, params, cancelToken);
          },
          wrap_vector_payload<::lsp::TextDocument_DocumentHighlightResult>{}));

  add_request_handler<::lsp::requests::TextDocument_PrepareRename>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_PrepareRename>(
          server, sharedServices, renameRequirement,
          [&sharedServices](const ::lsp::PrepareRenameParams &params,
                            const utils::CancellationToken &cancelToken) {
            return prepareRename(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_PrepareRenameResult>{}));

  add_request_handler<::lsp::requests::TextDocument_Rename>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_Rename>(
          server, sharedServices, renameRequirement,
          [&sharedServices](const ::lsp::RenameParams &params,
                            const utils::CancellationToken &cancelToken) {
            return rename(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_RenameResult>{}));

  add_request_handler<::lsp::requests::TextDocument_CodeAction>(
      handler,
      create_request_handler<::lsp::requests::TextDocument_CodeAction>(
          server, sharedServices, codeActionRequirement,
          [&sharedServices](const ::lsp::CodeActionParams &params,
                            const utils::CancellationToken &cancelToken) {
            return getCodeActions(sharedServices, params, cancelToken);
          },
          wrap_optional_payload<::lsp::TextDocument_CodeActionResult>{}));
}

} // namespace pegium
