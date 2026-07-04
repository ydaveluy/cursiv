#pragma once

#include <string>
#include <string_view>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/lsp/services/ServiceAccess.hpp>

namespace pegium::test {

using namespace pegium::parser;

struct NavigationEntry : pegium::NamedAstNode {};

struct NavigationUse : AstNode {
  reference<NavigationEntry> target;
};

struct NavigationMultiUse : AstNode {
  multi_reference<NavigationEntry> targets;
};

struct NavigationModel : AstNode {
  vector<pointer<NavigationEntry>> entries;
  vector<pointer<NavigationUse>> uses;
  vector<pointer<NavigationMultiUse>> multiUses;
};

class NavigationParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<NavigationEntry> EntryRule{
      "Entry", "entry"_kw + assign<&NavigationEntry::name>(ID)};
  Rule<NavigationUse> UseRule{"Use",
                              "use"_kw + assign<&NavigationUse::target>(ID)};
  Rule<NavigationMultiUse> MultiUseRule{
      "MultiUse", "link"_kw + assign<&NavigationMultiUse::targets>(ID)};
  Rule<NavigationModel> ModelRule{
      "Model",
      some(append<&NavigationModel::entries>(EntryRule) |
           append<&NavigationModel::uses>(UseRule) |
           append<&NavigationModel::multiUses>(MultiUseRule))};
#pragma clang diagnostic pop
};

inline const pegium::Services *
lookup_services(const pegium::SharedServices &shared,
                std::string_view languageId) {
  for (const auto *coreServices : shared.serviceRegistry->all()) {
    if (coreServices != nullptr &&
        coreServices->languageMetaData.languageId == languageId) {
      const auto *services = as_services(coreServices);
      if (services != nullptr) {
        return services;
      }
    }
  }
  return nullptr;
}

inline TextOffset use_name_offset(const workspace::Document &document) {
  return static_cast<TextOffset>(document.textDocument().getText().rfind("Alpha"));
}

inline ::lsp::LocationLink link_to_element(const AstNode &element) {
  const auto *document = tryGetDocument(element);
  const auto cstNode = element.getCstNode();

  ::lsp::LocationLink link{};
  link.targetUri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  link.targetRange.start =
      document->textDocument().positionAt(cstNode.getBegin());
  link.targetRange.end =
      document->textDocument().positionAt(cstNode.getEnd());
  link.targetSelectionRange = link.targetRange;
  return link;
}

} // namespace pegium::test
