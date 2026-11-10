// One include, no template, no grammar: the generated module is all the
// application uses.
#include <cstdio>

#include "tiny/generated/Tiny/Module.hpp"

int main() {
  const auto parser = tiny::generated::Tiny::create_parser();
  const cursiv::ParseResult ok = parser->parse("1 + 2 + 39");
  const cursiv::ParseResult bad = parser->parse("1 +");
  if (ok.cst().recovered() || !bad.cst().recovered()) {
    std::fputs("consumer: the module did not read what it was handed\n",
               stderr);
    return 1;
  }
  std::puts("consumer: the module reads");
  return 0;
}
