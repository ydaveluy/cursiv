// One random grammar's test, compiled against the parser the build generated
// for it, or against the generator's refusal. `--may-refuse` passes the
// latter: a long run draws grammars the generator rightly refuses.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "oracle/rgram_lane.hpp"

#if __has_include(RGRAM_MODULE)
#include RGRAM_MODULE
#include "cursiv/engine/parser/LanguageParser.hpp"

int main() {
  namespace g = RGRAM_NS::generated::G;
  const auto parser = g::createParser();
  const auto reflection = g::createAstReflection();
  auto tables = g::kTables;
  tables.predict = nullptr;
  const cursiv::engine::Language language{tables, g::kAst, g::kGrammar};
  const cursiv::engine::LanguageParser election(language);
  return cursiv::rgram_lane(RGRAM_SEED, *parser, *reflection, 128,
                            g::kTables.predict ? &election : nullptr);
}
#else
int main(int argc, char** argv) {
  std::ifstream in(RGRAM_REFUSED);
  const std::string why(std::istreambuf_iterator<char>(in), {});
  std::printf("rgram %d: refused by the generator\n%s", RGRAM_SEED,
              why.c_str());
  return argc > 1 && std::string_view(argv[1]) == "--may-refuse" ? 0 : 1;
}
#endif
