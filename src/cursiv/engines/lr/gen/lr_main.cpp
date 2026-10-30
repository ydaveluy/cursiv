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
#include <utility>
#include <vector>

#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/engines/lr/gen/LrAmbiguity.hpp"
#include "cursiv/engines/lr/gen/LrFinalTable.hpp"
#include "cursiv/engines/lr/gen/LrIdentity.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/gen/lr_completion_sites.hpp"
#include "cursiv/engines/lr/gen/lr_emit_parser.hpp"
#include "cursiv/engines/lr/gen/lr_emit_table.hpp"
#include "cursiv/engines/lr/gen/lr_entry_rows.hpp"
#include "cursiv/engines/lr/gen/lr_fault_message.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/shared/gen/AsWritten.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/AstRowOf.hpp"
#include "cursiv/engines/shared/gen/CommonArtifacts.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/GenCode.hpp"
#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/gen_report.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

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
using View = cursiv::LrView<G>;

namespace {

using cursiv::exit_status;
using cursiv::GenCode;

using Built = cursiv::LrBuildTable;

constexpr std::string_view kStampRowBits = "--stamp-row-bits=";
constexpr std::string_view kGroupLines = "--group-lines=";
constexpr std::string_view kOptionalGate = "--optional-gate=";

bool count_to(std::string_view text, unsigned most, unsigned& out,
              unsigned least = 1) {
  const char* const last = text.data() + text.size();
  unsigned v = 0;
  const auto [end, ec] = std::from_chars(text.data(), last, v);
  if (ec != std::errc{} || end != last || v < least || v > most) return false;
  out = v;
  return true;
}

struct Args {
  // The row width of a record's stamp a lane forces, where not zero.
  unsigned stampRowBits = 0;
  // The lines a group of states holds, which a lane may force down.
  unsigned groupLines = cursiv::kLrGroupLines;
  unsigned optionalGate = cursiv::kOptionalGate;

  // A lane's option: false where it is none, or its count is out of range.
  bool take(std::string_view arg) {
    if (arg.starts_with(kStampRowBits))
      return count_to(arg.substr(kStampRowBits.size()),
                      cursiv::StampLayout{}.rowBits, stampRowBits);
    if (arg.starts_with(kGroupLines))
      return count_to(arg.substr(kGroupLines.size()),
                      std::numeric_limits<unsigned>::max(), groupLines);
    if (arg.starts_with(kOptionalGate))
      return count_to(arg.substr(kOptionalGate.size()), 15, optionalGate, 0);
    return false;
  }
};

// The wider graph is built where the core's merge arbitrated or mixed implied
// tokens, and stands on `lr_wider_stands`.
std::unique_ptr<Built> build(const cursiv::LrLowered& low) {
  auto core = std::make_unique<Built>(cursiv::lr_build_automaton(low));
  if (core->nMergeInduced != 0 || core->nImpliedMixed != 0) {
    auto wide = std::make_unique<Built>(cursiv::lr_build_wide(low, core.get()));
    if (cursiv::lr_wider_stands(*core, *wide)) core = std::move(wide);
  }
  return core;
}

// A shape the core cannot hold is refused before anything is built.
bool refuse_ungated(const cursiv::LrLowered& low) {
  using Lowering = cursiv::LrLowering<G>;
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

// Whether the graph is refused before anything is derived from it. The stamp
// row width a lane forces is applied to it here, while it can still change.
bool refuse_graph(Built& a, unsigned stampRowBits) {
  return cursiv::refuse_unbuilt(CURSIV_GEN_NAME, a,
                                a.cfg.lowerRule < a.cfg.nnts
                                    ? a.cfg.nt_name(a.cfg.lowerRule)
                                    : std::string_view{}) ||
         cursiv::refuse_empty_match(CURSIV_GEN_NAME, empty_match_terminal(a)) ||
         cursiv::refuse_unstamped(CURSIV_GEN_NAME, a.stamp,
                                  a.writes.nchains + a.nfused, a.nstates,
                                  stampRowBits);
}

// What the build found, in one line, and why a grammar is not deterministic
// when it is not.
void report(const Built& a, unsigned probeCuts) {
  std::printf(
      "%s: states=%u terms=%u nts=%u prods=%u actions=%u dfa=%u chains=%u "
      "acts=%u identity=%s\n",
      CURSIV_GEN_NAME, a.nstates, a.cfg.nterms, a.cfg.nnts, a.cfg.nprods,
      a.nactions, a.ndfa, a.writes.nchains, a.writes.nacts,
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
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const std::uint32_t s = queue[i];
    for (const cursiv::LrAction& x : a.action_row(s))
      a.each_landing(x, [&](std::uint16_t to) { reach(s, to, x.term); });
    for (const auto& e : a.gotoRows[s])
      a.each_goto_landing(
          e, [&](std::uint16_t to) { reach(s, to, cursiv::nt_sym(e.nt)); });
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

int run(int argc, char** argv) try {
  Args args;
  const char* const dir = cursiv::gen_out_dir(
      argc, argv,
      "[--stamp-row-bits=1..16] [--group-lines=N] [--optional-gate=0..15]",
      [&](std::string_view arg) { return args.take(arg); });
  if (dir == nullptr) return exit_status(GenCode::usage);
  cursiv::GrammarImage image = cursiv::grammar_image<View>();
  if (cursiv::refuse_self_bounded_arms(CURSIV_GEN_NAME, image))
    return exit_status(GenCode::refused);
  auto desc = cursiv::kLrLower<G>;
  desc.optionalGate = args.optionalGate;
  const auto low = cursiv::lr_lower(image, desc);
  if (refuse_ungated(low))
    return exit_status(GenCode::refused);

  auto automaton = build(low);
  if (refuse_graph(*automaton, args.stampRowBits))
    return exit_status(GenCode::refused);
  const auto registry = cursiv::ast_registry<G, cursiv::LrLowering<G>>(
      CURSIV_GEN_STRINGIFY(CURSIV_GEN_GRAMMAR));
  const auto entries = cursiv::lr_entry_table(*automaton);
  const auto records = cursiv::lr_record_gens(entries, registry.rows);
  // From here the table is read-only.
  const cursiv::LrFinalTable done = cursiv::LrFinalTable::make(
      std::move(automaton), cursiv::lr_completion_sites(records));
  const Built& a = done.table();

  unsigned probeCuts = 0;
  const auto states = cursiv::lr_emit_parser_files(done, CURSIV_GEN_NS,
                                                   &probeCuts, args.groupLines);
  if (const GenCode wrote = cursiv::write_common_artifacts(
          dir,
          {.table = cursiv::lr_emit_table(done, CURSIV_GEN_NS),
           .parser = cursiv::lr_emit_parser_decl(done, CURSIV_GEN_NS),
           .parserUnits = states,
           .ns = CURSIV_GEN_NS,
           .header = CURSIV_GEN_HEADER,
           .dump = cursiv::dump_compiled_indexed<View>(),
           .registry = registry,
           .entries = entries,
           .records = records,
           .terminals = cursiv::record_terminals(a.cfg),
           .lang = {CURSIV_GEN_ID, cursiv::split(CURSIV_GEN_EXTENSIONS),
                    cursiv::split(CURSIV_GEN_FILE_NAMES)},
           .parserHeader = "cursiv/engines/lr/runtime/LrLanguageParser.hpp",
           .parserClass = "LrLanguageParser"});
      wrote != GenCode::ok)
    return exit_status(wrote);

  report(a, probeCuts);
  report_ambiguities(low.cfg);
  const auto& cfg = low.cfg;
  cursiv::report_warnings(
      CURSIV_GEN_NAME, image, cfg, a.cfg, [&](std::uint16_t r) {
        return r < cfg.nnts ? cfg.nt_name(r) : std::string_view{};
      });
  if (std::getenv("CURSIV_CONFLICTS") != nullptr) report_each_conflict(a);
  return exit_status(GenCode::ok);
} catch (const std::exception& e) {
  std::fprintf(stderr, "%s: refused: %s\n", CURSIV_GEN_NAME, e.what());
  return exit_status(GenCode::refused);
}

}  // namespace CURSIV_GEN_SCOPE

#ifndef CURSIV_GEN_NO_MAIN
int main(int argc, char** argv) {
  return CURSIV_GEN_SCOPE::run(argc, argv);
}
#endif
