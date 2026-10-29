// The lexer of an LL table as source: every mode's walk spelled as a ladder of
// rungs, the shared emitter's, its end the shared runtime's.
#include "cursiv/engines/ll/gen/ll_emit_walk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlLexView.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/LexWalkEmitter.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"

namespace cursiv {
namespace {

struct LlWalkEmitter : EmitOutput {
  explicit LlWalkEmitter(const LlBuiltProgram& built)
      : a(built),
        view{built},
        dfa(std::span<const std::uint8_t, 256>(built.cls),
            {built.dfaNext.data(), std::size_t{built.ndfa} * built.ncls},
            built.ncls, built.ndfa) {}

  const LlBuiltProgram& a;
  const LlLexView view;
  LexDfaEmitter dfa;
  LexRunEmitter runs;

  bool relays() const { return !a.relays.empty(); }

  // What the rungs ask.
  bool blind(unsigned st) const {
    return lex_run_blind(view, static_cast<std::uint16_t>(st));
  }
  bool plain_run(unsigned st) const {
    return lex_plain_run(view, static_cast<std::uint16_t>(st));
  }
  int continuation_of(unsigned st, const std::uint8_t* spelled) const {
    return lex_continuation_of(view, static_cast<std::uint16_t>(st), spelled);
  }
  void emit_bound_exit(unsigned, const std::uint8_t*) {}
  void emit_match(unsigned st, std::string_view ind) {
    if (a.dfaAccept[st] != 0)
      fmt("{}match.take(kTape, {}u, src, i, len, far);\n", ind, st);
  }
  void emit_relay_call(unsigned st, std::string_view ind) {
    if (!relays()) return;
    for (auto k = a.dfaRelay[st]; k != kNoRelay; k = a.relays[k].next)
      fmt("{}relays.launch(kTape, {}u, src, len, start, i);\n", ind, k);
  }
  void emit_run_adv(const ByteSet& stay, std::string_view var,
                    std::string_view set) {
    runs.advance(*this, stay, var, set);
  }

  // What the walk around them asks.
  void walk_before(bool) {}
  // A session that backtracked reads the table's walk, which checkpoints.
  void walk_entry() {
    line("  (void)end;\n  (void)term;");
    if (!a.modeBacktrack.empty())
      raw(R"(  if (::cursiv::LexSuffixSession<
          ::cursiv::LlLexTape<kTable>>::has_backtracked())
    return ::cursiv::lex_walk(kTape, src, len, mode, i);
)");
  }
  void emit_rows_loop() {
    raw("Wtab:\n  {\n");
    if (!relays()) line("    bool relays = false;");
    raw(R"(    ::cursiv::lex_walk_rows(kTape, src, len, start, st, i, far, match,
        relays, [&](::std::uint16_t state, ::cursiv::TextOffset& at) {
          if (!::cursiv::lex_run_blind(kTape, state)) return;
          while (at < len &&
                 kTape.step(state, static_cast<unsigned char>(src[at])) == state)
            ++at;
        });
  }
)");
  }
  void walk_foot() {
    line("Wdone:");
    if (relays())
      line(
          "  return ::cursiv::lex_walk_end(kTape, src, len, mode, start, i, "
          "far, match,\n                              relays);");
    else
      line(
          "  {\n    bool none = false;\n"
          "    return ::cursiv::lex_walk_end(kTape, src, len, mode, start, i, "
          "far, match,\n                                none);\n  }");
    line("}");
  }

  void emit() {
    lex_emit_byte_runs(*this);
    line("inline constexpr ::cursiv::LlLexTape<kTable> kTape{};");
    if (relays())
      line(
          "using Relays = "
          "::cursiv::LexRelayRun<::cursiv::LlLexTape<kTable>>;");
    // Every state keeps its labels, reached or not.
    raw(R"(
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-label"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-label"
#endif
)");
    lex_emit_walk(*this, dfa, *this, {a.modeStart.data(), a.nmodes}, relays(),
                  "");
    if (relays())
      lex_emit_relay_runs(*this, dfa, runs, view, a.relays, "relay_run");
    raw(R"(#ifdef __clang__
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
)");
  }
};

}  // namespace

std::string ll_emit_walk_includes(const LlBuiltProgram& a) {
  std::string s =
      "#include <array>\n#include <bit>\n#include <cstring>\n\n"
      "#include \"Table.hpp\"\n"
      "#include \"cursiv/engines/ll/runtime/ll_matcher.hpp\"\n"
      "#include \"cursiv/engines/shared/runtime/LexByteRun.hpp\"\n"
      "#include \"cursiv/engines/shared/runtime/LexMatch.hpp\"\n"
      "#include \"cursiv/engines/shared/runtime/LexWalk.hpp\"\n";
  if (!a.modeBacktrack.empty())
    s += "#include \"cursiv/engines/shared/runtime/LexSuffixSession.hpp\"\n";
  if (!a.relays.empty())
    s += "#include \"cursiv/engines/shared/runtime/LexRelayRun.hpp\"\n";
  return s;
}

std::string ll_emit_walk(const LlBuiltProgram& a) {
  LlWalkEmitter e(a);
  e.emit();
  return std::move(e.out);
}

}  // namespace cursiv
