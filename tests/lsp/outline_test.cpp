// The outline lists the nodes the root reaches: a node the grammar builds and
// no member takes is not a symbol of the document.

#include <lsp/types.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/test/Check.hpp"
#include "lsp/OutlineGrammar.hpp"
#include "outlinelr/generated/Lr/Module.hpp"

namespace {

struct World {
  struct Shared : cursiv::SharedLspServices {};
  struct Language : cursiv::LspServices {
    explicit Language(Shared& s)
        : cursiv::CoreServices(s), cursiv::LspServices(s) {}
  };

  Shared shared;
  Language* language = nullptr;
  unsigned minted = 0;

  World(std::unique_ptr<cursiv::Parser> parser,
        std::unique_ptr<cursiv::AstReflection> reflection) {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_lsp_services(shared);
    auto services = std::make_unique<Language>(shared);
    services->languageMetaData = {"outline", {".dn"}, {}};
    services->parser = std::move(parser);
    services->astReflection = std::move(reflection);
    services->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*services);
    cursiv::install_default_core_services(*services);
    cursiv::install_default_lsp_services(*services);
    language = services.get();
    shared.serviceRegistry->registerServices(std::move(services));
  }

  std::vector<lsp::DocumentSymbol> outline(std::string text) {
    auto document = shared.documentFactory->fromString(
        std::move(text), "file:///w/a" + std::to_string(minted++) + ".dn");
    cursiv::Document* const raw = document.get();
    shared.documents->addDocument(std::move(document));
    cursiv::Document* const set[] = {raw};
    shared.documentBuilder->build(set);
    return language->documentSymbolProvider->getSymbols(*raw);
  }
};

void the_outline_lists_the_named_nodes_under_an_unnamed_root(
    std::unique_ptr<cursiv::Parser> parser,
    std::unique_ptr<cursiv::AstReflection> reflection) {
  World world(std::move(parser), std::move(reflection));
  const auto symbols = world.outline("@a | @b");
  if (symbols.size() != 2)
    for (const auto& symbol : symbols)
      std::printf("outline lists %s\n", symbol.name.c_str());
  CHECK(symbols.size() == 2);
  CHECK(symbols.size() == 2 && symbols[0].name == "a" &&
        symbols[1].name == "b");
}

}  // namespace

int main() {
  namespace g = toutline::generated;
  the_outline_lists_the_named_nodes_under_an_unnamed_root(
      g::Lr::create_parser(), g::Lr::create_ast_reflection());
  return cursiv::test::report("outline_test");
}
