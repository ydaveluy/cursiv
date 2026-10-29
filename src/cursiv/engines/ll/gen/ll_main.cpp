// The generator. One source, one target per grammar: the macros name the
// grammar and its header, and the output is everything that grammar derives --
// its table, its descent, its AST, its grammar answers, its text and its
// module -- as C++ source.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_ast_registry.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/gen/ll_emit_parser.hpp"
#include "cursiv/engines/ll/gen/ll_emit_table.hpp"
#include "cursiv/engines/ll/gen/ll_entry_rows.hpp"
#include "cursiv/engines/shared/gen/AsWritten.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/CommonArtifacts.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/GenCode.hpp"
#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/gen_report.hpp"
#include "cursiv/engines/shared/gen/write_source.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"

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
using View = cursiv::LlView<G>;

namespace {

using cursiv::exit_status;
using cursiv::GenCode;

// What the build found, in one line: how many choices one token settles, how
// many an automaton the build closed settles, and how many are left to the
// parse -- the last is the only number that costs anything at run time.
void report(const cursiv::LlBuiltProgram& a) {
  unsigned single = 0, staged = 0;
  for (std::uint16_t d = 0; d < a.cfg.ndecisions; ++d) {
    if (a.cfg.decisions[d].how == cursiv::LlPredict::single) ++single;
    if (a.cfg.decisions[d].how == cursiv::LlPredict::staged) ++staged;
  }
  std::printf(
      "%s: terms=%u rules=%u nodes=%u ll1=%u staged=%u adaptive=%u open=%u "
      "modes=%u dfa=%u atn=%u\n",
      CURSIV_GEN_NAME, a.cfg.nterms, a.cfg.nrules, a.cfg.nnodes, single, staged,
      a.nadaptive, a.nopen, a.nmodes, a.ndfa, a.nstates);
}

// A count the tables address with sixteen bits, refused under this grammar's
// name where it overruns them.
bool fits(std::size_t n, const char* what) {
  return cursiv::ll_fits(CURSIV_GEN_NAME, n, what);
}

}  // namespace

int run(int argc, char** argv) try {
  cursiv::LlBuildOptions buildOpt;
  const char* dir = nullptr;
  bool bad = false;
  for (int i = 1; i < argc && !bad; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--no-staged")
      buildOpt.staticPredict = false;
    else if (arg.starts_with("-"))
      bad = true;
    else
      dir = argv[i];
  }
  if (bad || dir == nullptr) {
    std::fprintf(stderr, "usage: %s [--no-staged] <out-dir>\n", argv[0]);
    return exit_status(GenCode::usage);
  }

  cursiv::GrammarImage image = cursiv::grammar_image<View>();
  if (cursiv::refuse_self_bounded_arms(CURSIV_GEN_NAME, image))
    return exit_status(GenCode::refused);

  const cursiv::LlLowered& low = cursiv::LlLowering<G>::table();
  // The counters the lowering fills are sixteen bits wide while the store
  // behind them is not: a grammar that overran one would have kept writing at
  // an offset that wrapped, so the store's own extent is what says so.
  if (!fits(low.cfg.names.size(), "name characters") ||
      !fits(low.cfg.sets.size(), "byte classes") ||
      !fits(low.cfg.lex.size(), "token graph nodes") ||
      !fits(low.cfg.lexKid.size(), "token graph edges") ||
      !fits(low.cfg.terms.size(), "terminals") ||
      !fits(low.cfg.rules.size(), "rules") ||
      !fits(low.cfg.decisions.size(), "decisions") ||
      !fits(low.cfg.climbs.size(), "precedence tables") ||
      !fits(low.cfg.climbOps.size(), "precedence operators"))
    return exit_status(GenCode::refused);
  // What the emitters cannot write is refused before anything is written:
  // the sentence names the rule, and no table exists to name it in. From here
  // the program is read-only.
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(low, buildOpt), CURSIV_GEN_NAME,
      cursiv::AsWritten<cursiv::LlLowering<G>::rules>::of);
  if (!done) return exit_status(GenCode::refused);
  const cursiv::LlBuiltProgram& a = done->program();
  // The descent is spelled first: a rule whose calls its frame cannot name is
  // refused before anything is written.
  const auto steps = cursiv::ll_emit_parser_files(*done, CURSIV_GEN_NS);

  const std::string out(dir);
  if (!cursiv::write_source(out + "/Table.hpp",
                            cursiv::ll_emit_table(*done, CURSIV_GEN_NS)))
    return exit_status(GenCode::table);
  if (!cursiv::write_source(out + "/Parser.hpp",
                            cursiv::ll_emit_parser_decl(CURSIV_GEN_NS)))
    return exit_status(GenCode::parser);
  for (const auto& [fname, text] : steps) {
    if (!cursiv::write_source(out + "/" + fname, text))
      return exit_status(GenCode::parserFiles);
  }

  const auto registry = cursiv::ll_ast_registry<G>(CURSIV_GEN_STRINGIFY(CURSIV_GEN_GRAMMAR));
  std::vector<cursiv::RecordGen> records;
  const auto entries = cursiv::ll_entry_table(*done, registry.rows, records);
  const auto terminals = cursiv::record_terminals(a.cfg);
  const auto dump = cursiv::dump_compiled_indexed<View>();
  const cursiv::LanguageIdentity lang{CURSIV_GEN_ID,
                                      cursiv::split(CURSIV_GEN_EXTENSIONS),
                                      cursiv::split(CURSIV_GEN_FILE_NAMES)};
  if (const auto wrote = cursiv::write_common_artifacts(
          out,
          {.ns = CURSIV_GEN_NS,
           .header = CURSIV_GEN_HEADER,
           .dump = dump,
           .registry = registry,
           .entries = entries,
           .records = records,
           .terminals = terminals,
           .lang = lang,
           .parserHeader = "cursiv/engines/ll/runtime/LlLanguageParser.hpp",
           .parserClass = "LlLanguageParser"});
      wrote != GenCode::ok)
    return exit_status(wrote);

  report(a);
  cursiv::report_unbounded_conditions(
      CURSIV_GEN_NAME, a.cfg, [&](std::uint16_t r) {
        return r < a.cfg.nrules ? a.cfg.rule_name(r) : std::string_view{};
      });
  cursiv::report_incomplete_lex_checks(CURSIV_GEN_NAME, a.cfg);
  cursiv::report_grammar_warnings(CURSIV_GEN_NAME, image);
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
