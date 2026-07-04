#include <gtest/gtest.h>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/semantic/AbstractSignatureHelpProvider.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

struct SignatureNode : AstNode {
  string name;
};

class SignatureParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return RootRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<SignatureNode> RootRule{"Root", assign<&SignatureNode::name>(ID)};
#pragma clang diagnostic pop
};

class TestSignatureHelpProvider final : public AbstractSignatureHelpProvider {
public:
  explicit TestSignatureHelpProvider(const pegium::Services &services)
      : AbstractSignatureHelpProvider(services) {}

protected:
  std::optional<::lsp::SignatureHelp>
  getSignatureFromElement(
      const AstNode &element,
      const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    const auto *node = dynamic_cast<const SignatureNode *>(&element);
    if (node == nullptr) {
      return std::nullopt;
    }

    ::lsp::SignatureInformation signature{};
    signature.label = node->name + "(...)";

    ::lsp::SignatureHelp help{};
    help.signatures.push_back(std::move(signature));
    help.activeSignature = 0u;
    help.activeParameter = 0u;
    return help;
  }
};

TEST(AbstractSignatureHelpProviderTest, ExposesTriggerCharactersAndDelegatesToAstElement) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SignatureParser>(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("signature.test"), "test", "call");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestSignatureHelpProvider provider(*services);

  const auto options = provider.signatureHelpOptions();
  ASSERT_TRUE(options.triggerCharacters.has_value());
  ASSERT_EQ(options.triggerCharacters->size(), 1u);
  EXPECT_EQ((*options.triggerCharacters)[0], "(");

  ::lsp::SignatureHelpParams params{};
  params.position.line = 0;
  params.position.character = 1;

  const auto help = provider.provideSignatureHelp(
      *document, params, utils::default_cancel_token);
  ASSERT_TRUE(help.has_value());
  ASSERT_EQ(help->signatures.size(), 1u);
  EXPECT_EQ(help->signatures[0].label, "call(...)");
}

} // namespace
} // namespace pegium
