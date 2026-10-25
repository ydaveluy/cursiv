#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/semantic/InlayHintProvider.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "tiny/generated/Tiny/Module.hpp"

struct Symbols final : cursiv::DocumentSymbolProvider {
  std::vector<lsp::DocumentSymbol> getSymbols(
      const cursiv::Document&,
      const cursiv::CancellationToken&) const override {
    lsp::DocumentSymbol symbol;
    symbol.name = "sum";
    symbol.kind = lsp::SymbolKind::Variable;
    return {std::move(symbol)};
  }
};

struct Formatter final : cursiv::Formatter {
  std::vector<lsp::TextEdit> formatDocument(
      const cursiv::Document& document,
      const lsp::DocumentFormattingParams& params,
      const cursiv::CancellationToken&) const override {
    if (document.state.load() != cursiv::DocumentState::Validated ||
        params.textDocument.uri.toString() != document.uri ||
        params.options.tabSize != 2)
      throw std::runtime_error("formatter did not receive a prepared document");
    return {lsp::TextEdit{lsp::Range{{0, 0}, {0, 0}}, " "}};
  }
  std::vector<lsp::TextEdit> formatDocumentRange(
      const cursiv::Document&, const lsp::DocumentRangeFormattingParams&,
      const cursiv::CancellationToken&) const override {
    return {};
  }
};

struct Hints final : cursiv::InlayHintProvider {
  std::vector<lsp::InlayHint> getInlayHints(
      const cursiv::Document&, lsp::Range,
      const cursiv::CancellationToken&) const override {
    return {};
  }
};

bool register_language(cursiv::SharedCoreServices& shared) {
  auto services = std::make_unique<cursiv::LspServices>(
      dynamic_cast<cursiv::SharedLspServices&>(shared));
  cursiv::install_default_core_services(*services);
  tiny::generated::Tiny::install_generated_module(*services);
  cursiv::install_default_lsp_services(*services);
  services->documentSymbolProvider = std::make_unique<Symbols>();
  services->formatter = std::make_unique<Formatter>();
  services->inlayHintProvider = std::make_unique<Hints>();
  shared.serviceRegistry->registerServices(std::move(services));
  return true;
}

int main(int argc, char** argv) {
  return cursiv::bench::run_language<cursiv::SharedLspServices,
                                     register_language>(argc, argv);
}
