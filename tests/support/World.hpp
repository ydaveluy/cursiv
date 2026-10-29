// The workspace a lane asks its services of, set up as a host sets one up --
// the shared defaults over an empty disk, then each language with its parser,
// its reflection, the defaults and the slots the lane writes itself -- the
// documents held and built in it, and what watches it work.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/LanguageMetaData.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"

namespace support {

// `tweak` replaces the slots the lane writes itself once the defaults are in,
// before the language is registered.
template <class Services, class Shared>
Services& add_language(Shared& shared, cursiv::LanguageMetaData meta,
                       std::unique_ptr<cursiv::Parser> parser,
                       std::unique_ptr<cursiv::AstReflection> reflection,
                       const std::function<void(Services&)>& tweak = {}) {
  auto language = std::make_unique<Services>(shared);
  language->languageMetaData = std::move(meta);
  language->parser = std::move(parser);
  language->astReflection = std::move(reflection);
  if constexpr (std::is_base_of_v<cursiv::LspServices, Services>)
    cursiv::install_default_lsp_services(*language);
  else
    cursiv::install_default_core_services(*language);
  if (tweak) tweak(*language);
  Services& added = *language;
  shared.serviceRegistry->registerServices(std::move(language));
  return added;
}

// Held by the store, not built.
inline cursiv::Document* add(const cursiv::SharedCoreServices& shared,
                             std::string_view uri, std::string text) {
  auto document = shared.documentFactory->fromString(std::move(text), uri);
  cursiv::Document* const raw = document.get();
  shared.documents->addDocument(std::move(document));
  return raw;
}

// Held, then built to the end in a set of its own.
inline cursiv::Document* built(const cursiv::SharedCoreServices& shared,
                               std::string_view uri, std::string text) {
  cursiv::Document* const set[] = {add(shared, uri, std::move(text))};
  shared.documentBuilder->build(set);
  return set[0];
}

// Held, then built in one set: a reference resolves against what its
// neighbours export, so the set reaches the index together.
inline std::vector<cursiv::Document*> built(
    const cursiv::SharedCoreServices& shared,
    std::vector<std::pair<std::string, std::string>> sources) {
  std::vector<cursiv::Document*> documents;
  for (auto& [uri, text] : sources)
    documents.push_back(add(shared, uri, std::move(text)));
  shared.documentBuilder->build(documents);
  return documents;
}

// One parser per generated module, made on first use and shared.
template <auto Create>
const cursiv::Parser& parser_of() {
  static const std::unique_ptr<cursiv::Parser> parser = Create();
  return *parser;
}

// A parser that hands every parse on, and counts them.
class CountingParser final : public cursiv::Parser {
 public:
  explicit CountingParser(std::unique_ptr<const cursiv::Parser> parser)
      : _parser(std::move(parser)) {}
  const cursiv::Grammar& getGrammar() const noexcept override {
    return _parser->getGrammar();
  }
  cursiv::ParseResult parse(
      std::string_view text,
      const cursiv::CancellationToken& token) const override {
    ++calls;
    return _parser->parse(text, token);
  }
  mutable std::atomic<unsigned> calls{0};

 private:
  std::unique_ptr<const cursiv::Parser> _parser;
};

// The sink a host writes: it keeps what it is told.
struct CaptureSink final : cursiv::ObservabilitySink {
  std::vector<cursiv::Observation> seen;
  void publish(const cursiv::Observation& observation) noexcept override {
    try {
      seen.push_back(observation);
    } catch (...) {
    }
  }
};

// One installed in `shared` in place of its sink, kept by address.
inline CaptureSink& capture_observations(cursiv::SharedCoreServices& shared) {
  auto sink = std::make_unique<CaptureSink>();
  CaptureSink& kept = *sink;
  shared.observabilitySink = std::move(sink);
  return kept;
}

template <class Shared = cursiv::SharedCoreServices>
struct World {
  Shared shared;

  World() {
    if constexpr (std::is_base_of_v<cursiv::SharedLspServices, Shared>)
      cursiv::install_default_shared_lsp_services(shared);
    else
      cursiv::install_default_shared_core_services(shared);
  }

  cursiv::Document* add(std::string_view uri, std::string text) const {
    return support::add(shared, uri, std::move(text));
  }
  cursiv::Document* built(std::string_view uri, std::string text) const {
    return support::built(shared, uri, std::move(text));
  }
  std::vector<cursiv::Document*> built(
      std::vector<std::pair<std::string, std::string>> sources) const {
    return support::built(shared, std::move(sources));
  }
};

}  // namespace support
