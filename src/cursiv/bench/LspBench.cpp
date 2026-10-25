#include <cctype>
#include <optional>
#include <span>
#include <vector>

#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/lsp/actions/CodeActionProvider.hpp"
#include "cursiv/lsp/actions/CodeLensProvider.hpp"
#include "cursiv/lsp/actions/DocumentLinkProvider.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
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
#include "cursiv/lsp/semantic/InlayHintProvider.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/semantic/SignatureHelpProvider.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/lsp/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv::bench {
namespace {

template <class T>
std::size_t result_count(const T& result) {
  if constexpr (requires { result.size(); })
    return result.size();
  else if constexpr (requires { result.items.size(); })
    return result.items.size();
  else if constexpr (requires { result.data.size(); })
    return result.data.size() / 5;
  else if constexpr (requires { result.signatures.size(); })
    return result.signatures.size();
  else
    return 1;
}

template <class T>
std::size_t result_count(const std::optional<T>& result) {
  return result ? result_count(*result) : 0;
}

// One warmup per probe, then record every repetition. Reply counts are read
// after stopping the clock; destruction is also outside that interval.
template <class Action>
void measure(Row& row, const char* name, bool installed, int iterations,
             std::size_t probes, Action action) {
  ServiceMeasurement result;
  result.name = name;
  if (installed) {
    result.probes = probes;
    for (std::size_t probe = 0; probe < probes; ++probe) {
      (void)action(probe);
      for (int i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto reply = action(probe);
        const double elapsed = ms_since(start);
        result.record(elapsed);
        if (i == 0) result.results += result_count(reply);
      }
    }
  }
  row.lsp.push_back(std::move(result));
}

// Spread probes over word starts, where navigation and hover can answer,
// rather than arbitrary bytes inside UTF-8 sequences or whitespace. The last
// position is EOF for completion. Conversion to UTF-16 is outside the clock.
std::vector<lsp::Position> positions(const Document& document) {
  const auto text = document.textDocument().getText();
  std::vector<TextOffset> starts;
  bool previous = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    const bool word = std::isalnum(c) != 0 || c == '_' || c >= 0x80;
    if (word && !previous) starts.push_back(static_cast<TextOffset>(i));
    previous = word;
  }
  std::vector<lsp::Position> result;
  const auto count = std::min<std::size_t>(starts.size(), 9);
  for (std::size_t i = 0; i < count; ++i) {
    const auto index = count == 1 ? 0 : i * (starts.size() - 1) / (count - 1);
    result.push_back(to_lsp(document.textDocument().positionAt(starts[index])));
  }
  result.push_back(to_lsp(document.textDocument().positionAt(
      static_cast<TextOffset>(text.size()))));
  return result;
}

}  // namespace

void lsp_stages(int iterations, SharedLspServices& shared,
                const LspServices& services, const Document& document,
                Row& row) {
  const auto carets = positions(document);
  const lsp::Range range{lsp::Position{0, 0}, carets.back()};
  const auto n = carets.size();
  const auto once = [&](const char* name, bool installed, auto action) {
    measure(row, name, installed, iterations, 1, action);
  };
  const auto sweep = [&](const char* name, bool installed, auto action) {
    measure(row, name, installed, iterations, n, action);
  };

  once("documentSymbol", services.documentSymbolProvider != nullptr,
       [&](std::size_t) {
         return services.documentSymbolProvider->getSymbols(document);
       });
  once("workspace/symbol", shared.workspaceSymbolProvider != nullptr,
       [&](std::size_t) {
         return shared.workspaceSymbolProvider->getSymbols("");
       });
  once("foldingRange", services.foldingRangeProvider != nullptr,
       [&](std::size_t) {
         return services.foldingRangeProvider->getFoldingRanges(document);
       });
  once("selectionRange", services.selectionRangeProvider != nullptr,
       [&](std::size_t) {
         return services.selectionRangeProvider->getSelectionRanges(document,
                                                                    carets);
       });
  once("semanticTokens/full", services.semanticTokenProvider != nullptr,
       [&](std::size_t) {
         return services.semanticTokenProvider->semanticHighlight(document);
       });
  once("semanticTokens/range", services.semanticTokenProvider != nullptr,
       [&](std::size_t) {
         return services.semanticTokenProvider->semanticHighlightRange(document,
                                                                       range);
       });

  lsp::DocumentFormattingParams format;
  format.textDocument.uri = lsp::Uri::parse(document.uri);
  format.options.tabSize = 2;
  format.options.insertSpaces = true;
  lsp::DocumentRangeFormattingParams formatRange;
  formatRange.textDocument = format.textDocument;
  formatRange.options = format.options;
  formatRange.range = range;
  once("formatting", services.formatter != nullptr, [&](std::size_t) {
    return services.formatter->formatDocument(document, format);
  });
  once("rangeFormatting", services.formatter != nullptr, [&](std::size_t) {
    return services.formatter->formatDocumentRange(document, formatRange);
  });
  const auto onType = services.formatter != nullptr
                          ? services.formatter->formatOnTypeOptions()
                          : std::nullopt;
  lsp::DocumentOnTypeFormattingParams typeFormat;
  typeFormat.textDocument = format.textDocument;
  typeFormat.options = format.options;
  typeFormat.position = carets.back();
  if (onType) typeFormat.ch = onType->firstTriggerCharacter;
  once("onTypeFormatting", onType.has_value(), [&](std::size_t) {
    return services.formatter->formatDocumentOnType(document, typeFormat);
  });
  sweep(
      "completion", services.completionProvider != nullptr, [&](std::size_t i) {
        return services.completionProvider->getCompletion(document, carets[i]);
      });
  sweep("hover", services.hoverProvider != nullptr, [&](std::size_t i) {
    return services.hoverProvider->getHoverContent(document, carets[i]);
  });
  sweep(
      "definition", services.definitionProvider != nullptr, [&](std::size_t i) {
        return services.definitionProvider->getDefinition(document, carets[i]);
      });
  sweep("declaration", services.declarationProvider != nullptr,
        [&](std::size_t i) {
          return services.declarationProvider->getDeclaration(document,
                                                              carets[i]);
        });
  sweep("implementation", services.implementationProvider != nullptr,
        [&](std::size_t i) {
          return services.implementationProvider->getImplementation(document,
                                                                    carets[i]);
        });
  sweep("typeDefinition", services.typeDefinitionProvider != nullptr,
        [&](std::size_t i) {
          return services.typeDefinitionProvider->getTypeDefinition(document,
                                                                    carets[i]);
        });
  sweep("references", services.referencesProvider != nullptr,
        [&](std::size_t i) {
          return services.referencesProvider->findReferences(document,
                                                             carets[i], true);
        });
  sweep("documentHighlight", services.documentHighlightProvider != nullptr,
        [&](std::size_t i) {
          return services.documentHighlightProvider->getDocumentHighlight(
              document, carets[i]);
        });
  sweep("prepareRename", services.renameProvider != nullptr,
        [&](std::size_t i) {
          return services.renameProvider->prepareRename(document, carets[i]);
        });
  sweep("rename", services.renameProvider != nullptr, [&](std::size_t i) {
    return services.renameProvider->rename(document, carets[i], "benchRenamed");
  });
  lsp::SignatureHelpParams signature;
  signature.textDocument.uri = lsp::Uri::parse(document.uri);
  sweep("signatureHelp", services.signatureHelpProvider != nullptr,
        [&](std::size_t i) {
          signature.position = carets[i];
          return services.signatureHelpProvider->provideSignatureHelp(
              document, signature);
        });
  once("inlayHint", services.inlayHintProvider != nullptr, [&](std::size_t) {
    return services.inlayHintProvider->getInlayHints(document, range);
  });
  once("codeLens", services.codeLensProvider != nullptr, [&](std::size_t) {
    return services.codeLensProvider->provideCodeLens(document);
  });
  once("documentLink", services.documentLinkProvider != nullptr,
       [&](std::size_t) {
         return services.documentLinkProvider->getDocumentLinks(document);
       });
  lsp::CodeActionParams actions;
  actions.textDocument.uri = lsp::Uri::parse(document.uri);
  actions.range = range;
  for (const Diagnostic& diagnostic : document.diagnostics) {
    lsp::Diagnostic converted;
    converted.range = to_lsp(document.textDocument().rangeAt(
        diagnostic.range.begin, diagnostic.range.end));
    converted.message = diagnostic.message;
    actions.context.diagnostics.push_back(std::move(converted));
  }
  once("codeAction", services.codeActionProvider != nullptr, [&](std::size_t) {
    return services.codeActionProvider->getCodeActions(document, actions);
  });
  sweep("prepareCallHierarchy", services.callHierarchyProvider != nullptr,
        [&](std::size_t i) {
          return services.callHierarchyProvider->prepareCallHierarchy(
              document, carets[i]);
        });
  sweep("prepareTypeHierarchy", services.typeHierarchyProvider != nullptr,
        [&](std::size_t i) {
          return services.typeHierarchyProvider->prepareTypeHierarchy(
              document, carets[i]);
        });
}

}  // namespace cursiv::bench
