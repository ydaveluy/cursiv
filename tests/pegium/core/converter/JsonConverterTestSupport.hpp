#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>

namespace pegium::test {

using namespace pegium::parser;

struct Person : pegium::NamedAstNode {};

struct Greeting : AstNode {
  reference<Person> person;
};

struct Model : AstNode {
  vector<pointer<Person>> persons;
  vector<pointer<Greeting>> greetings;
};

class JsonConverterParser final : public PegiumParser {
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
  Terminal<> COMMENT{"COMMENT", "/* comment */"_kw};
  Skipper skipper = SkipperBuilder().ignore(WS).hide(COMMENT).build();

  Rule<Person> PersonRule{"Person",
                          "person"_kw + assign<&Person::name>("John"_kw)};

  Rule<Greeting> GreetingRule{"Greeting",
                              "hello"_kw + assign<&Greeting::person>("John"_kw)};

  Rule<Model> ModelRule{
      "Model",
      append<&Model::persons>(PersonRule) + append<&Model::greetings>(GreetingRule)};
#pragma clang diagnostic pop
};

inline constexpr std::string_view kReferenceInput = "person John hello John";
inline constexpr std::string_view kReferenceCstInput =
    "person John /* comment */ hello John";

inline bool register_language(pegium::SharedCoreServices &sharedServices) {
  auto services = pegium::test::make_uninstalled_core_services<JsonConverterParser>(
      sharedServices, "json-converter", {".json-converter"});
  pegium::installDefaultCoreServices(*services);
  installDefaultCoreServices(*services);
  sharedServices.serviceRegistry->registerServices(std::move(services));
  return true;
}

inline std::shared_ptr<workspace::Document>
open_reference_document(pegium::SharedCoreServices &sharedServices) {
  return pegium::test::open_and_build_document(
      sharedServices, pegium::test::make_file_uri("reference.json-converter"),
      "json-converter", std::string(kReferenceInput));
}

inline std::shared_ptr<workspace::Document>
open_reference_cst_document(pegium::SharedCoreServices &sharedServices) {
  return pegium::test::open_and_build_document(
      sharedServices, pegium::test::make_file_uri("reference-cst.json-converter"),
      "json-converter", std::string(kReferenceCstInput));
}

} // namespace pegium::test
