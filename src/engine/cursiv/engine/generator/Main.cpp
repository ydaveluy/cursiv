// The generator of one language: this source is compiled once per grammar,
// which the build names with the `CURSIV_GEN_*` macros.
#ifdef CURSIV_GEN_INCLUDE
#include CURSIV_GEN_INCLUDE
#else
#include CURSIV_GEN_HEADER
#endif

#include "cursiv/engine/generator/Generate.hpp"
#include "cursiv/engine/grammar/View.hpp"

#ifndef CURSIV_GEN_SCOPE
#define CURSIV_GEN_SCOPE cursiv_generator
#endif
#define CURSIV_GEN_TEXT_OF(x) #x
#define CURSIV_GEN_TEXT(x) CURSIV_GEN_TEXT_OF(x)

namespace CURSIV_GEN_SCOPE {

int run(int argc, char** argv) {
  using G = CURSIV_GEN_GRAMMAR;
  cursiv::engine::gen::GrammarImage image =
      cursiv::engine::gen::grammar_image<cursiv::engine::gen::View<G>>();
  return cursiv::engine::gen::generate(
      image, cursiv::engine::gen::kLowerDesc<G>,
      {CURSIV_GEN_NAME, CURSIV_GEN_NS, CURSIV_GEN_HEADER, CURSIV_GEN_TEXT(CURSIV_GEN_GRAMMAR),
       CURSIV_GEN_ID, CURSIV_GEN_EXTENSIONS, CURSIV_GEN_FILE_NAMES},
      argc, argv);
}

}  // namespace CURSIV_GEN_SCOPE

#ifndef CURSIV_GEN_NO_MAIN
int main(int argc, char** argv) { return CURSIV_GEN_SCOPE::run(argc, argv); }
#endif
