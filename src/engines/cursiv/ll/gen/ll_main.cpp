// The generator, one target per grammar named by the macros: it writes
// everything that grammar derives as C++ source, and its keywords into the
// TextMate file `--textmate` names.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/ll/gen/LlFinalProgram.hpp"
#include "cursiv/ll/gen/LlLowering.hpp"
#include "cursiv/ll/gen/ll_build.hpp"
#include "cursiv/ll/gen/ll_emit_parser.hpp"
#include "cursiv/ll/gen/ll_emit_table.hpp"
#include "cursiv/ll/gen/ll_entry_rows.hpp"
#include "cursiv/shared/gen/AsWritten.hpp"
#include "cursiv/shared/gen/AstRegistry.hpp"
#include "cursiv/shared/gen/AstRowOf.hpp"
#include "cursiv/shared/gen/CommonArtifacts.hpp"
#include "cursiv/shared/gen/EmitRecords.hpp"
#include "cursiv/shared/gen/GenCode.hpp"
#include "cursiv/shared/gen/LanguageIdentity.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/TextMateKeywords.hpp"
#include "cursiv/shared/gen/gen_report.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/utils/dump.hpp"

#ifdef CURSIV_GEN_INCLUDE
#include CURSIV_GEN_INCLUDE
#else
#include CURSIV_GEN_HEADER
#endif

#ifndef CURSIV_GEN_SCOPE
#define CURSIV_GEN_SCOPE cursiv_generator
#endif
namespace CURSIV_GEN_SCOPE {

using G = CURSIV_GEN_GRAMMAR;
using View = cursiv::engine::LlView<G>;

namespace {

using cursiv::engine::exit_status;
using cursiv::engine::GenCode;

// What the build found, in one line: how many choices one token decides, how
// many an automaton the build closed decides, and how many are left to the
// parse. Only the last has a run-time cost.
void report(const cursiv::engine::LlBuiltProgram& a) {
  unsigned single = 0, staged = 0;
  for (std::uint16_t d = 0; d < a.cfg.ndecisions; ++d) {
    if (a.cfg.decisions[d].how == cursiv::engine::LlPredict::single) ++single;
    if (a.cfg.decisions[d].how == cursiv::engine::LlPredict::staged) ++staged;
  }
  std::printf(
      "%s: terms=%u rules=%u nodes=%u ll1=%u staged=%u adaptive=%u open=%u "
      "modes=%u dfa=%u atn=%u\n",
      CURSIV_GEN_NAME, a.cfg.nterms, a.cfg.nrules, a.cfg.nnodes, single, staged,
      a.nadaptive, a.nopen, a.nmodes, a.ndfa, a.nstates);
}

}  // namespace

int run(int argc, char** argv) try {
  cursiv::engine::LlBuildOptions buildOpt;
  const cursiv::engine::GenPaths paths = cursiv::engine::gen_paths(
      argc, argv, "[--no-staged]", [&](std::string_view arg) {
        buildOpt.staticPredict = false;
        return arg == "--no-staged";
      });
  if (paths.dir == nullptr) return exit_status(GenCode::usage);

  cursiv::detail::GrammarImage image = cursiv::detail::grammar_image<View>();
  if (cursiv::engine::refuse_self_bounded_arms(CURSIV_GEN_NAME, image))
    return exit_status(GenCode::refused);

  const cursiv::engine::LlLowered& low = cursiv::engine::LlLowering<G>::table();
  // What the emitters cannot write is refused before anything is written:
  // the sentence names the rule, and no table exists to name it in. From here
  // the program is read-only.
  const auto done = cursiv::engine::LlFinalProgram::make(
      cursiv::engine::ll_build_program(low, buildOpt), CURSIV_GEN_NAME,
      cursiv::engine::AsWritten<cursiv::engine::LlLowering<G>::rules>::of);
  if (!done) return exit_status(GenCode::refused);
  const cursiv::engine::LlBuiltProgram& a = done->program();
  // A rule whose calls its frame cannot name is refused before anything is
  // written.
  auto table = cursiv::engine::ll_emit_table(*done, CURSIV_GEN_NS);
  const auto steps =
      cursiv::engine::ll_emit_parser_files(*done, CURSIV_GEN_NS, table);

  const auto registry =
      cursiv::engine::ast_registry<G, cursiv::engine::LlLowering<G>>(
          CURSIV_GEN_STRINGIFY(CURSIV_GEN_GRAMMAR));
  std::vector<cursiv::engine::RecordGen> records;
  const auto entries =
      cursiv::engine::ll_entry_table(*done, registry.rows, records);
  if (const auto wrote = cursiv::engine::write_common_artifacts(
          paths.dir,
          {.table = std::move(table.header),
           .parser = cursiv::engine::ll_emit_parser_decl(CURSIV_GEN_NS),
           .parserUnits = steps,
           .ns = CURSIV_GEN_NS,
           .header = CURSIV_GEN_HEADER,
           .dump = cursiv::engine::dump_compiled_indexed<View>(),
           .registry = registry,
           .entries = entries,
           .records = records,
           .terminals = cursiv::engine::record_terminals(a.cfg),
           .lang = {CURSIV_GEN_ID, cursiv::engine::split(CURSIV_GEN_EXTENSIONS),
                    cursiv::engine::split(CURSIV_GEN_FILE_NAMES)},
           .textmate = paths.textmate,
           .keywords = cursiv::engine::textmate_keywords(a.cfg),
           .parserHeader = "cursiv/ll/runtime/LlLanguageParser.hpp",
           .parserClass = "LlLanguageParser"});
      wrote != GenCode::ok)
    return exit_status(wrote);

  report(a);
  cursiv::engine::report_warnings(
      CURSIV_GEN_NAME, image, a.cfg, a.cfg,
      [&](std::uint16_t r) { return a.cfg.rule_name(r); });
  return exit_status(GenCode::ok);
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s: refused: %s\n", CURSIV_GEN_NAME, error.what());
  return exit_status(GenCode::refused);
}

}  // namespace CURSIV_GEN_SCOPE

#ifndef CURSIV_GEN_NO_MAIN
int main(int argc, char** argv) {
  return CURSIV_GEN_SCOPE::run(argc, argv);
}
#endif
