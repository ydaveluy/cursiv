#pragma once

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "requirements/core/ast.hpp"

namespace req {

inline constexpr auto unquote = [](std::string_view t) {
  return t.substr(1, t.size() - 2);
};

inline constexpr auto contactKw = cursiv::ikw<"contact">.doc<
    "The person to ask about this file.">();
inline constexpr auto environmentKw = cursiv::ikw<"environment">.doc<
    "Declares an environment that requirements and tests can apply to.">();
inline constexpr auto reqKw = cursiv::ikw<"req">.doc<
    "Declares a requirement: its name, then its text.">();
inline constexpr auto applicableKw = cursiv::ikw<"applicable">.doc<
    "Starts the environments this applies to: `applicable for A, B`.">();
inline constexpr auto forKw = cursiv::ikw<"for">.doc<
    "Lists the environments after `applicable`.">();
inline constexpr auto tstKw = cursiv::ikw<"tst">.doc<
    "Declares a test: its name, then the requirements it covers.">();
inline constexpr auto testfileKw = cursiv::ikw<"testfile">.doc<
    "The file that implements the test.">();
inline constexpr auto testsKw = cursiv::ikw<"tests">.doc<
    "Lists the requirements this test covers.">();

struct Rules : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  // terminal ID: [A-Za-z_] [0-9A-Za-z_]*;
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // terminal STRING:
  // ('"' (('\\' .) | (!'"' .))* '"') | ("'" (('\\' .) | (!"'" .))* "'");
  struct STRING : terminal<as<unquote>(
                      (lit<"\""> + many((lit<"\\"> + any) | cr<R"(^"\\\r\n)">)
                       + lit<"\"">)
                      | (lit<"'"> + many((lit<"\\"> + any) | cr<R"(^'\\\r\n)">)
                         + lit<"'">))> {};

  // Contact returns Contact: 'contact'i ':' userName=STRING;
  struct Contact : rule<contactKw + lit<":">
                        + assign<&ast::Contact::userName>(ref<STRING>)> {};

  // Environment returns Environment:
  // 'environment'i name=ID ':' description=STRING;
  struct Environment
      : rule<environmentKw + assign<&ast::Environment::name>(ref<ID>)
             + lit<":"> + assign<&ast::Environment::description>(ref<STRING>)> {
  };

  // Requirement returns Requirement:
  // 'req'i name=ID text=STRING
  // ('applicable'i 'for'i environments+=ID (',' environments+=ID)*)?;
  struct Requirement
      : rule<reqKw + assign<&ast::Requirement::name>(ref<ID>)
             + assign<&ast::Requirement::text>(ref<STRING>)
             + option(applicableKw + forKw
                      + some(append<&ast::Requirement::environments>(ref<ID>),
                             lit<",">))> {};

  // RequirementModel returns RequirementModel:
  // contact=Contact? environments+=Environment* requirements+=Requirement+;
  struct RequirementModel
      : rule<option(assign<&ast::RequirementModel::contact>(ref<Contact>))
             + many(
                 append<&ast::RequirementModel::environments>(ref<Environment>))
             + some(append<&ast::RequirementModel::requirements>(
                 ref<Requirement>))> {};

  // Test returns Test:
  // 'tst'i name=ID ('testfile'i '=' testFile=STRING)?
  // 'tests'i requirements+=ID (',' requirements+=ID)*
  // ('applicable'i 'for'i environments+=ID (',' environments+=ID)*)?;
  struct Test : rule<tstKw + assign<&ast::Test::name>(ref<ID>)
                     + option(testfileKw + lit<"=">
                              + assign<&ast::Test::testFile>(ref<STRING>))
                     + testsKw
                     + some(append<&ast::Test::requirements>(ref<ID>), lit<",">)
                     + option(applicableKw + forKw
                              + some(append<&ast::Test::environments>(ref<ID>),
                                     lit<",">))> {};

  // TestModel returns TestModel: contact=Contact? tests+=Test+;
  struct TestModel : rule<option(assign<&ast::TestModel::contact>(ref<Contact>))
                          + some(append<&ast::TestModel::tests>(ref<Test>))> {};

  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

struct Requirements : Rules {
  using entry = RequirementModel;
};
struct Tests : Rules {
  using entry = TestModel;
};

}  // namespace req
