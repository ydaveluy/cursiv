// The smoke test of the language: build the container the way a workspace
// does, read the sample, and check the tree. No framework -- the exit code is
// the verdict, and ctest reads it.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "@CURSIV_NEW_ID@: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  std::ifstream file(CURSIV_SAMPLE);
  if (!file) return fail("the sample document is not readable");
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

  // The whole language in one call: the defaults, then `install_core_module`.
  const auto language = cursiv::create_language<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
      @CURSIV_NEW_ID@::install_core_module>();

  const cursiv::ParseResult result = language.language.parser->parse(text);
  if (!result || !result.diagnostics.empty())
    return fail("the sample does not parse cleanly");

  const auto* const root =
      cursiv::ast_cast<@CURSIV_NEW_ID@::ast::Model>(result.value);
  if (root == nullptr) return fail("the parse built no model");

  const auto& model = *root;
  if (model.persons.size() != 2 || model.greetings.size() != 2)
    return fail("the model does not hold what the sample declares");

  std::puts("@CURSIV_NEW_ID@: the sample parses");
  return 0;
}
