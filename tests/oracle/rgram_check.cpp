// One random grammar's lane, compiled against the parser the build generated
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

int main() {
  namespace g = RGRAM_NS::generated::G;
  const auto parser = g::create_parser();
  const auto reflection = g::create_ast_reflection();
  return cursiv::rgram_lane(RGRAM_SEED, *parser, *reflection);
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
