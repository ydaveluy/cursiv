// The lexer of an LL table as source: the shared `LexStateBlocks` writes each
// mode's walk, and the shared runtime ends it.
#include "cursiv/ll/gen/ll_emit_walk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/ll/gen/LlLexView.hpp"
#include "cursiv/shared/gen/EmitTable.hpp"
#include "cursiv/shared/gen/LexWalkEmitter.hpp"

namespace cursiv::engine {
namespace {

struct LlWalkEmitter : LexStateBlocks<LlLexView> {
  explicit LlWalkEmitter(const LlBuiltProgram& built)
      : LexStateBlocks(LlLexView{built}),
        a(built),
        dfa(std::span<const std::uint8_t, 256>(built.cls),
            {built.dfaNext.data(), std::size_t{built.ndfa} * built.ncls},
            built.ncls, built.ndfa) {}

  const LlBuiltProgram& a;
  LexDfaEmitter dfa;

  bool relays() const { return !a.relays.empty(); }

  // A checked state exits on its keyword directly when the terminal it
  // accepts reserves nothing and no other terminal it accepts has a boundary.
  void emit_bound_exit(unsigned st, const std::uint8_t* spelled) {
    if (a.dfaAccept[st] == 0) return;
    const auto top = static_cast<SymbolId>(a.dfaAccept[st] - 1u);
    for (const LlLexAccept& x : a.choices(static_cast<std::uint16_t>(st)))
      if (x.term != top && x.bound != kNoSet) return;
    if (!a.dfaResSet[st].test(top)) emit_keyword_exit(st, spelled, "kTable");
  }

  void walk_before(bool) const { /* nothing to emit before the walk */ }
  // A session that backtracked reads the table's walk, which checkpoints.
  void walk_entry() {
    line("  (void)end;\n  (void)term;");
    if (!a.modeBacktrack.empty())
      write(R"(  if (::cursiv::engine::LexSuffixSession<
          ::cursiv::engine::LlLexTape<kTable>>::has_backtracked())
    return ::cursiv::engine::lex_walk(kTape, src, len, mode, i);
)");
  }
  void emit_rows_loop() {
    line("Wtab:\n  {");
    if (!relays()) line("    bool relays = false;");
    write(
        R"(    ::cursiv::engine::lex_walk_rows(kTape, src, len, start, st, i, far, match,
        relays, [&](::std::uint16_t state, ::cursiv::TextOffset& at) {
          if (!::cursiv::engine::lex_run_blind(kTape, state)) return;
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
          "  return ::cursiv::engine::lex_walk_end(kTape, src, len, mode, "
          "start, i, "
          "far, match,\n                              relays);");
    else
      line(
          "  {\n    bool none = false;\n"
          "    return ::cursiv::engine::lex_walk_end(kTape, src, len, mode, "
          "start, i, "
          "far, match,\n                                none);\n  }");
    line("}");
  }

  void emit() {
    lex_emit_byte_runs(*this);
    line("inline constexpr ::cursiv::engine::LlLexTape<kTable> kTape{};");
    if (relays())
      line(
          "using Relays = "
          "::cursiv::engine::LexRelayRun<::cursiv::engine::LlLexTape<kTable>>"
          ";");
    // Every state keeps its labels, reached or not.
    write(R"(
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
    write(R"(#ifdef __clang__
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
      "#include \"cursiv/ll/runtime/ll_matcher.hpp\"\n"
      "#include \"cursiv/shared/runtime/LexByteRun.hpp\"\n"
      "#include \"cursiv/shared/runtime/LexMatch.hpp\"\n"
      "#include \"cursiv/shared/runtime/LexWalk.hpp\"\n";
  if (!a.modeBacktrack.empty())
    s += "#include \"cursiv/shared/runtime/LexSuffixSession.hpp\"\n";
  if (!a.relays.empty())
    s += "#include \"cursiv/shared/runtime/LexRelayRun.hpp\"\n";
  return s;
}

std::string ll_emit_walk(const LlBuiltProgram& a) {
  LlWalkEmitter e(a);
  e.emit();
  return e.take();
}

}  // namespace cursiv::engine
