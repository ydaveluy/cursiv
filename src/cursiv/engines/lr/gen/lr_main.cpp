// The table builder, one target per grammar named by the macros: it writes
// everything that grammar derives as C++ source.
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/engines/lr/gen/LrAmbiguity.hpp"
#include "cursiv/engines/lr/gen/LrGuard.hpp"
#include "cursiv/engines/lr/gen/LrIdentity.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"
#include "cursiv/engines/lr/gen/lr_ast_registry.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/gen/lr_build_derived.hpp"
#include "cursiv/engines/lr/gen/lr_completion_sites.hpp"
#include "cursiv/engines/lr/gen/lr_emit_parser.hpp"
#include "cursiv/engines/lr/gen/lr_emit_table.hpp"
#include "cursiv/engines/lr/gen/lr_entry_rows.hpp"
#include "cursiv/engines/lr/gen/lr_fault_message.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/shared/gen/AsWritten.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/CommonArtifacts.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/GenCode.hpp"
#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/gen_report.hpp"
#include "cursiv/engines/shared/gen/write_source.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

#include CURSIV_GEN_HEADER

using G = CURSIV_GEN_GRAMMAR;
using View = cursiv::LrView<G>;

namespace {

using cursiv::exit_status;
using cursiv::GenCode;

using Built = cursiv::LrBuildTable;

constexpr std::string_view kStampRowBits = "--stamp-row-bits=";
constexpr std::string_view kGroupLines = "--group-lines=";

bool count_to(std::string_view text, unsigned most, unsigned& out) {
  const char* const last = text.data() + text.size();
  unsigned v = 0;
  const auto [end, ec] = std::from_chars(text.data(), last, v);
  if (ec != std::errc{} || end != last || v == 0 || v > most) return false;
  out = v;
  return true;
}

struct Args {
  // The row width of a record's stamp a lane forces, where not zero.
  unsigned stampRowBits = 0;
  // The lines a group of states holds, which a lane may force down.
  unsigned groupLines = cursiv::kLrGroupLines;
  const char* dir = nullptr;
  bool bad = false;
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc && !args.bad; ++i) {
    const std::string_view arg = argv[i];
    if (arg.starts_with(kStampRowBits))
      args.bad = !count_to(arg.substr(kStampRowBits.size()),
                           cursiv::StampLayout{}.rowBits, args.stampRowBits);
    else if (arg.starts_with(kGroupLines))
      args.bad =
          !count_to(arg.substr(kGroupLines.size()),
                    std::numeric_limits<unsigned>::max(), args.groupLines);
    else if (arg.starts_with("-"))
      args.bad = true;
    else
      args.dir = argv[i];
  }
  return args;
}

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--stamp-row-bits=1..16] [--group-lines=N] "
               "<out-dir>\n",
               argv0);
}

// The wider graph is built where the core's merge arbitrated or mixed implied
// tokens, and stands on `lr_wider_stands`.
std::unique_ptr<Built> build() {
  const cursiv::LrLowered& low = cursiv::LrLowering<G>::table();
  auto core = std::make_unique<Built>(cursiv::lr_build_automaton(low));
  if (core->nMergeInduced != 0 || core->nImpliedMixed != 0) {
    auto wide = std::make_unique<Built>(cursiv::lr_build_wide(low, core.get()));
    if (cursiv::lr_wider_stands(*core, *wide)) core = std::move(wide);
  }
  return core;
}

// A shape the core cannot hold is refused before anything is built.
bool refuse_ungated() {
  using Lowering = cursiv::LrLowering<G>;
  const auto& low = Lowering::table();
  // A lowering that refused a shape has its own sentence.
  if (!low.fault || low.cfg.lowerFail()) return false;
  const std::uint16_t nt = cursiv::lr_fault_rule(low);
  const std::string_view rule =
      nt < low.cfg.nnts ? low.cfg.nt_name(nt) : std::string_view{};
  cursiv::refuse_fault(
      CURSIV_GEN_NAME, low.fault,
      cursiv::lr_fault_message(low,
                               cursiv::AsWritten<Lowering::rules>::of(rule)));
  return true;
}

std::string_view empty_match_terminal(const Built& a) {
  return cursiv::empty_match_terminal(a, [&](std::uint16_t state) {
    return a.dfaAccept[state] != 0
               ? static_cast<cursiv::SymbolId>(a.dfaAccept[state] - 1)
               : cursiv::kInvalidSymbol;
  });
}

// The generated units: the first that fails names the exit code.
GenCode write_artifacts(const Built& a, const std::string& out,
                        const cursiv::CommonArtifacts& common,
                        unsigned groupLines, unsigned& probeCuts) {
  const cursiv::LrProgramLayout programs(a);
  if (!cursiv::write_source(out + "/Table.hpp",
                            cursiv::lr_emit_table(a, CURSIV_GEN_NS, programs)))
    return GenCode::table;
  if (!cursiv::write_source(out + "/Parser.hpp",
                            cursiv::lr_emit_parser_decl(a, CURSIV_GEN_NS)))
    return GenCode::parser;
  for (const auto& [fname, text] : cursiv::lr_emit_parser_files(
           a, CURSIV_GEN_NS, programs, &probeCuts, groupLines)) {
    if (!cursiv::write_source(out + "/" + fname, text))
      return GenCode::parserFiles;
  }
  return cursiv::write_common_artifacts(out, common);
}

// What the build found, in one line, and why a grammar is not deterministic
// when it is not.
void report(const Built& a, unsigned probeCuts) {
  std::printf(
      "%s: states=%u terms=%u nts=%u prods=%u actions=%u dfa=%u chains=%u "
      "acts=%u turns=%d identity=%s\n",
      CURSIV_GEN_NAME, a.nstates, a.cfg.nterms, a.cfg.nnts, a.cfg.nprods,
      a.nactions, a.ndfa, a.writes.nchains, a.writes.nacts,
      a.nconflicts > 0 || a.multiCand ? 1 : 0,
      a.identity == cursiv::LrIdentity::core ? "core" : "wide");
  if (probeCuts != 0 || a.nunminted != 0)
    std::printf("%s: gave way: probeCuts=%u unminted=%u\n", CURSIV_GEN_NAME,
                probeCuts, a.nunminted);
  if (a.modeFlagPool.sets.size() != 0)
    std::printf("%s: flagged modes: %zu sets\n", CURSIV_GEN_NAME,
                a.modeFlagPool.sets.size());
  if (a.nconflicts == 0 && a.nOwed == 0 && !a.multiCand) return;
  std::printf(
      "%s: conflicts=%u owed=%u ordered=%u mergeInduced=%u"
      " (state=%u term=%u) canonical=%u multiCand=%d\n",
      CURSIV_GEN_NAME, a.nconflicts, a.nOwed, a.nOrdered, a.nMergeInduced,
      a.mergeState, a.mergeTerm, a.ncanonical, a.multiCand ? 1 : 0);
  // The pair that asks for a candidate walk.
  if (a.multiCand) {
    const std::string_view from = a.cfg.term_name(a.multiTerm);
    std::printf("%s: candidates mode=%u '%.*s'", CURSIV_GEN_NAME, a.multiMode,
                static_cast<int>(from.size()), from.data());
    if (a.multiInto != 0) {
      const std::string_view into = a.cfg.term_name(a.multiInto);
      std::printf(" -> '%.*s'", static_cast<int>(into.size()), into.data());
    }
    if (!a.multiText.empty()) {
      std::string text;
      for (const unsigned char b : a.multiText) text += cursiv::spell_byte(b);
      std::printf(" on `%s`", text.c_str());
    }
    std::printf("\n");
  }
}

// What the lowered grammar shows ambiguous with no table, a warning each.
void report_ambiguities(const cursiv::LrBuildCfg& c) {
  constexpr std::size_t kShown = 8;
  const auto found = cursiv::lr_find_ambiguities(c);
  for (std::size_t i = 0; i < found.size() && i < kShown; ++i)
    std::fprintf(stderr, "%s: warning: %s\n", CURSIV_GEN_NAME,
                 cursiv::lr_ambiguity_text(c, found[i]).c_str());
  if (found.size() > kShown)
    std::fprintf(stderr, "%s: warning: and %zu more\n", CURSIV_GEN_NAME,
                 found.size() - kShown);
}

// Each conflict named, on demand (`CURSIV_CONFLICTS`), after the shortest
// run of symbols that leads the parse into its state.
void report_each_conflict(const Built& a) {
  constexpr auto none = ~std::uint32_t{0};
  std::vector<std::uint32_t> back(a.nstates, none), via(a.nstates, 0), queue{0};
  back[0] = 0;
  const auto reach = [&](std::uint32_t from, std::uint32_t to,
                         std::uint32_t sym) {
    if (back[to] != none) return;
    back[to] = from;
    via[to] = sym;
    queue.push_back(to);
  };
  const auto land = [&](std::uint32_t from, std::uint32_t guard,
                        std::uint32_t sym) {
    for (const std::uint16_t to : a.guards[guard].to)
      if (to != cursiv::LrGuard::kNoState) reach(from, to, sym);
  };
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const std::uint32_t s = queue[i];
    for (const cursiv::LrAction& x : a.action_row(s)) {
      if (x.kind == cursiv::LrActionKind::shift) reach(s, x.arg, x.term);
      if (x.kind == cursiv::LrActionKind::guard) land(s, x.arg, x.term);
    }
    for (const auto& e : a.gotoRows[s]) {
      reach(s, e.to, cursiv::nt_sym(e.nt));
      if (e.guard != 0) land(s, e.guard - 1u, cursiv::nt_sym(e.nt));
    }
  }
  const auto prefix = [&](std::uint32_t st) {
    std::vector<std::uint16_t> path;
    for (auto s = st; s != 0 && back[s] != none; s = back[s])
      path.push_back(static_cast<std::uint16_t>(via[s]));
    std::string out;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      if (!out.empty()) out += ' ';
      out += cursiv::is_term(*it)
                 ? "'" + std::string(a.cfg.term_name(*it)) + "'"
                 : std::string(a.cfg.nt_name(cursiv::nt_index(*it)));
    }
    return out;
  };
  for (std::uint32_t st = 0; st < a.nstates; ++st) {
    const cursiv::LrAction* const row = a.actions.data() + a.actionBase[st];
    cursiv::lr_each_run(
        row, a.actionCount[st], [&](std::size_t i, std::size_t j) {
          if (j - i < 2) return;
          const std::string_view t = a.cfg.term_name(row[i].term);
          std::printf("%s: state %u on '%.*s' --\n", CURSIV_GEN_NAME, st,
                      static_cast<int>(t.size()), t.data());
          if (const std::string after = prefix(st); !after.empty())
            std::printf("      after   %s\n", after.c_str());
          for (std::size_t k = i; k < j; ++k)
            if (row[k].kind == cursiv::LrActionKind::reduce)
              std::printf("      reduce  %s\n",
                          a.cfg.prod_text(a.cfg.prods[row[k].arg]).c_str());
            else
              std::printf("      shift   -> state %u\n", row[k].arg);
        });
  }
}

}  // namespace

int main(int argc, char** argv) try {
  const Args args = parse_args(argc, argv);
  if (args.bad || args.dir == nullptr) {
    usage(argv[0]);
    return exit_status(GenCode::usage);
  }
  if (cursiv::refuse_self_bounded_arms(CURSIV_GEN_NAME,
                                       cursiv::grammar_image<View>()) ||
      refuse_ungated())
    return exit_status(GenCode::refused);

  const auto automaton = build();
  Built& a = *automaton;
  if (cursiv::refuse_unbuilt(CURSIV_GEN_NAME, a,
                             a.cfg.lowerRule < a.cfg.nnts
                                 ? a.cfg.nt_name(a.cfg.lowerRule)
                                 : std::string_view{}) ||
      cursiv::refuse_empty_match(CURSIV_GEN_NAME, empty_match_terminal(a)) ||
      cursiv::refuse_unstamped(CURSIV_GEN_NAME, a.stamp,
                               a.writes.nchains + a.nfused, a.nstates,
                               args.stampRowBits))
    return exit_status(GenCode::refused);
  const auto registry = cursiv::lr_ast_registry<G>(CURSIV_GEN_STRINGIFY(CURSIV_GEN_GRAMMAR));
  const auto entries = cursiv::lr_entry_table(a);
  const auto records = cursiv::lr_record_gens(entries, registry.rows);
  cursiv::lr_derive_tables(a, cursiv::lr_completion_sites(records));

  const auto terminals = cursiv::record_terminals(a.cfg);
  const auto dump = cursiv::dump_compiled_indexed<View>();
  const cursiv::LanguageIdentity lang{CURSIV_GEN_ID,
                                      cursiv::split(CURSIV_GEN_EXTENSIONS),
                                      cursiv::split(CURSIV_GEN_FILE_NAMES)};
  unsigned probeCuts = 0;
  if (const GenCode wrote = write_artifacts(
          a, args.dir,
          {.ns = CURSIV_GEN_NS,
           .header = CURSIV_GEN_HEADER,
           .dump = dump,
           .registry = registry,
           .entries = entries,
           .records = records,
           .terminals = terminals,
           .lang = lang,
           .parserHeader = "cursiv/engines/lr/runtime/LrLanguageParser.hpp",
           .parserClass = "LrLanguageParser"},
          args.groupLines, probeCuts);
      wrote != GenCode::ok)
    return exit_status(wrote);

  report(a, probeCuts);
  report_ambiguities(cursiv::LrLowering<G>::table().cfg);
  const auto& cfg = cursiv::LrLowering<G>::table().cfg;
  cursiv::report_unbounded_conditions(
      CURSIV_GEN_NAME, cfg, [&](std::uint16_t r) {
        return r < cfg.nnts ? cfg.nt_name(r) : std::string_view{};
      });
  cursiv::report_grammar_warnings(CURSIV_GEN_NAME,
                                  cursiv::grammar_image<View>());
  if (std::getenv("CURSIV_CONFLICTS") != nullptr) report_each_conflict(a);
  return exit_status(GenCode::ok);
} catch (const cursiv::LowerRefused& e) {
  std::fprintf(stderr, "%s: refused: %s\n", CURSIV_GEN_NAME, e.what());
  return exit_status(GenCode::refused);
} catch (const std::exception& e) {
  std::fprintf(stderr, "%s: %s\n", CURSIV_GEN_NAME, e.what());
  return exit_status(GenCode::refused);
}
