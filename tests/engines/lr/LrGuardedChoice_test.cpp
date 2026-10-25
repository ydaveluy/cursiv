#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/GuardedChoiceGrammar.hpp"
#include "groupguard/generated/G/Module.hpp"

namespace {

namespace g = tgroupguard::generated;

struct Reader {
  std::unique_ptr<cursiv::Parser> parser = nullptr;
  std::unique_ptr<cursiv::AstReflection> reflection = nullptr;

  [[nodiscard]] std::string tree(const cursiv::ParseResult& result) const {
    std::string out;
    if (result.value != nullptr)
      cursiv::test::ast_of(*reflection, *result.value, out);
    return out;
  }
};

// Representative empty guarded readings and refusals must retain their tree.
void a_text_no_condition_admits_is_repaired_not_lost(const Reader& named) {
  for (const std::string_view text : {"()", "(\n)", "( )", "(x)", "(@)", "(", ")"}) {
    const auto result = named.parser->parse(text);
    CHECK(result.accepted && result.value != nullptr);
  }
  for (const auto& [text, want] :
       {std::pair<std::string_view, std::string_view>{"()",
                                                      "Doc{mark=Slow{} }"},
        {"(\n)", "Doc{mark=Fast{} }"},
        {"( \n)", "Doc{mark=Fast{} }"}}) {
    const auto r = named.parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty() && !r.cst().recovered());
    CHECK(named.tree(r) == want);
  }
  const auto repaired = named.parser->parse("( )");
  CHECK(repaired.value != nullptr && !repaired.diagnostics.empty() &&
        repaired.cst().recovered());
}

}  // namespace

int main() {
  const Reader named{g::G::create_parser(),
                     g::G::create_ast_reflection()};
  a_text_no_condition_admits_is_repaired_not_lost(named);
  return cursiv::test::report("LrGuardedChoice_test");
}
