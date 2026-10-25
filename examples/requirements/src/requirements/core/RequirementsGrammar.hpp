#pragma once

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "requirements/core/ast.hpp"

namespace req {

inline constexpr auto unquote = [](std::string_view t) {
  return t.substr(1, t.size() - 2);
};
using Unquoted = cursiv::text_conv<unquote>;

struct Rules : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  // terminal ID: [A-Za-z_] [0-9A-Za-z_]*;
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // terminal STRING:
  // ('"' (('\\' .) | (!'"' .))* '"') | ("'" (('\\' .) | (!"'" .))* "'");
  struct STRING
      : terminal<(lit<"\""> + many((lit<"\\"> + any) | cr<R"(^"\\\r\n)">) +
                  lit<"\"">) |
                     (lit<"'"> + many((lit<"\\"> + any) | cr<R"(^'\\\r\n)">) +
                      lit<"'">),
                 Unquoted> {};

  // Contact returns Contact: 'contact'i ':' userName=STRING;
  struct ContactRule : rule<ikw<"contact"> + lit<":"> +
                            assign<&ast::Contact::userName>(ref<STRING>)> {};

  // Environment returns Environment:
  // 'environment'i name=ID ':' description=STRING;
  struct EnvironmentRule
      : rule<ikw<"environment"> + assign<&ast::Environment::name>(ref<ID>) +
             lit<":"> + assign<&ast::Environment::description>(ref<STRING>)> {};

  // Requirement returns Requirement:
  // 'req'i name=ID text=STRING
  // ('applicable'i 'for'i environments+=ID (',' environments+=ID)*)?;
  struct RequirementRule
      : rule<ikw<"req"> + assign<&ast::Requirement::name>(ref<ID>) +
             assign<&ast::Requirement::text>(ref<STRING>) +
             option(ikw<"applicable"> + ikw<"for"> +
                    some(append<&ast::Requirement::environments>(ref<ID>),
                         lit<",">))> {};

  // RequirementModel returns RequirementModel:
  // contact=Contact? environments+=Environment* requirements+=Requirement+;
  struct RequirementModelRule
      : rule<option(assign<&ast::RequirementModel::contact>(ref<ContactRule>)) +
             many(append<&ast::RequirementModel::environments>(
                 ref<EnvironmentRule>)) +
             some(append<&ast::RequirementModel::requirements>(
                 ref<RequirementRule>))> {};

  // Test returns Test:
  // 'tst'i name=ID ('testfile'i '=' testFile=STRING)?
  // 'tests'i requirements+=ID (',' requirements+=ID)*
  // ('applicable'i 'for'i environments+=ID (',' environments+=ID)*)?;
  struct TestRule
      : rule<ikw<"tst"> + assign<&ast::Test::name>(ref<ID>) +
             option(ikw<"testfile"> + lit<"="> +
                    assign<&ast::Test::testFile>(ref<STRING>)) +
             ikw<"tests"> +
             some(append<&ast::Test::requirements>(ref<ID>), lit<",">) +
             option(
                 ikw<"applicable"> + ikw<"for"> +
                 some(append<&ast::Test::environments>(ref<ID>), lit<",">))> {};

  // TestModel returns TestModel: contact=Contact? tests+=Test+;
  struct TestModelRule
      : rule<option(assign<&ast::TestModel::contact>(ref<ContactRule>)) +
             some(append<&ast::TestModel::tests>(ref<TestRule>))> {};

  using ignore = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

struct Requirements : Rules {
  using entry = RequirementModelRule;
};
struct Tests : Rules {
  using entry = TestModelRule;
};

}  // namespace req
