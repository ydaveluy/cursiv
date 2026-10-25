// The hot path of a grammar written out as C++ source: one label per state,
// every table constant folded into the code. The repair stays shared and
// re-enters through `drive_state`.
#include "cursiv/engines/lr/gen/lr_emit_parser.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrDfaEmitter.hpp"
#include "cursiv/engines/lr/gen/LrPeek.hpp"
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrFusedRow.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/lr/table/LrProgramRows.hpp"
#include "cursiv/engines/lr/table/LrReducePlan.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/GapConditionEmitter.hpp"
#include "cursiv/engines/shared/gen/GapEmitter.hpp"
#include "cursiv/engines/shared/gen/GapReach.hpp"
#include "cursiv/engines/shared/gen/LexicalPredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/NamedHead.hpp"
#include "cursiv/engines/shared/gen/PredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

namespace {

struct LrParserEmitter : EmitOutput {
  LrParserEmitter(const LrBuildTable& table, const LrProgramLayout& layout)
      : a(table),
        gotos(table.gotoRows, table.cfg.nnts),
        dfa(table.cls, {table.dfaNext.data(), table.dfaNext.size()}, table.ncls,
            table.ndfa),
        programs(layout) {}

  const LrBuildTable& a;
  const LrGotoColumns gotos;
  LrDfaEmitter dfa;
  // Whether a mode covers another: the token in hand then survives the
  // change, and a miss re-lexes.
  bool any_sub() const {
    for (unsigned x = 0; x < a.nmodes; ++x)
      for (unsigned y = 0; y < a.nmodes; ++y)
        if (x != y && a.mode_covers(x, y)) return true;
    return false;
  }
  // Whether a state may keep its token across a change of mode: an indented
  // grammar reads its token first, so no state asks.
  bool subs = false;

  bool any_recorded() const {
    for (unsigned t = 0; t < a.cfg.nterms; ++t)
      if (a.cfg.terms[t].recorded) return true;
    return false;
  }

  // A table that narrowed the row names its stamp layout at every move that
  // writes a record.
  bool narrowed() const { return a.stamp != StampLayout{}; }
  std::string_view stamp_args() const {
    return narrowed() ? "<kTable.stamp>" : "";
  }
  std::string_view stamp_more() const {
    return narrowed() ? ", kTable.stamp" : "";
  }

  // What `f` writes, taken back out of `out`.
  template <class F>
  std::string capture(F&& f) {
    const std::size_t at = out.size();
    f();
    std::string text = out.substr(at);
    out.resize(at);
    return text;
  }

  // `decl = {cells};`, `none` standing in for no cell; a column is sixteen
  // cells a line.
  template <class Cell>
  void array(std::string_view decl, std::size_t n, Cell&& cell,
             std::string_view none = "", bool column = false) {
    fmt("{} = {{", decl);
    for (std::size_t i = 0; i < n; ++i) {
      if (column && i % 16 == 0) out += '\n';
      if (!column && i != 0) out += ',';
      cell(i);
      if (column) out += ',';
    }
    if (n == 0) raw(none);
    line("};");
  }

  void set64(const ByteSet& s) {
    out += '{';
    for (int w = 0; w < 4; ++w) {
      if (w) out += ',';
      out += std::to_string(s.w[w]);
      out += "ull";
    }
    out += '}';
  }

  // The states split into groups, one function each: a jump that leaves its
  // group is a tail call into the group it lands in, and an exit returns.
  unsigned groupLines = kLrGroupLines;
  unsigned ngroups = 1;
  std::vector<unsigned> groupOf{};  // per state, then per reduce lead
  std::vector<unsigned> localOf{};  // a state's index among its group's entries
  std::vector<std::vector<std::string>> entries{};  // per group, `G` first
  std::vector<std::map<std::string, unsigned, std::less<>>> entryAt{};

  unsigned owner(char kind, unsigned n) const {
    return groupOf[kind == 'R' ? a.nstates + n : n];
  }
  // A jump to label `kind n` is written `@J<kind><n>;` and logged before the
  // groups exist; `enter` then registers its entry, `resolved` spells it.
  struct Jump {
    char kind;
    unsigned n;
  };
  std::vector<Jump> jumpLog{};
  struct Piece {
    std::string text;
    std::size_t from = 0, to = 0;  // its jumps, in `jumpLog`
  };
  template <class F>
  Piece piece(F&& f) {
    const std::size_t from = jumpLog.size();
    std::string text = capture(f);
    return {std::move(text), from, jumpLog.size()};
  }
  std::string jump(char kind, unsigned n) {
    jumpLog.push_back({kind, n});
    return std::format("@J{}{};", kind, n);
  }
  // Entries are numbered in the order `enter` meets them.
  void enter(unsigned g, const Piece& p) {
    for (std::size_t i = p.from; i < p.to; ++i) {
      const auto [kind, n] = jumpLog[i];
      const unsigned h = owner(kind, n);
      if (h == g) continue;
      auto label = std::format("{}{}", kind, n);
      const auto fresh = entryAt[h].try_emplace(
          label, static_cast<unsigned>(entries[h].size()));
      if (fresh.second) entries[h].push_back(std::move(label));
    }
  }
  std::string resolved(std::string_view text, unsigned g) const {
    std::string r;
    r.reserve(text.size());
    std::size_t at = 0;
    for (std::size_t k = text.find("@J"); k != std::string_view::npos;
         k = text.find("@J", at)) {
      r.append(text.substr(at, k - at));
      const char kind = text[k + 2];
      unsigned n = 0;
      const char* const end = text.data() + text.size();
      const char* const stop = std::from_chars(text.data() + k + 3, end, n).ptr;
      at = static_cast<std::size_t>(stop - text.data()) + 1;
      const unsigned h = owner(kind, n);
      if (h == g)
        r += std::format("goto {}{};", kind, n);
      else
        r += std::format("CURSIV_MUSTTAIL return states_{}(c, {}u);", h,
                         entryAt[h].at(std::format("{}{}", kind, n)));
    }
    r.append(text.substr(at));
    return r;
  }
  std::string leave(std::string_view exit) const {
    return std::format("return ::cursiv::LrExit::{};", exit);
  }
  // What one state probes at its entry, at most: past either cap the rest
  // fall through to the mode's lexer.
  static constexpr unsigned kLrProbeLits = 64;
  static constexpr unsigned kLrProbeNamed = 16;
  unsigned probeCuts = 0;
  // Marked 1: candidates for any token; marked 2: for a token that ends the
  // text. `sweepMode` is either.
  bool multiMode(unsigned m) const { return a.modeMulti[m] == 1; }
  bool eofMode(unsigned m) const { return a.modeMulti[m] == 2; }
  bool sweepMode(unsigned m) const { return a.modeMulti[m] != 0; }
  // Whether a mode flags a terminal that a stack may refuse.
  bool flags(unsigned m) const { return a.flags_mode(m); }
  // Whether a mode's ladder stops short, so its walk ends in the flat DFA rows.
  bool anyPartialLadder() const {
    for (unsigned m = 0; m < a.nmodes; ++m)
      if (walk_partial(m)) return true;
    return false;
  }

  const LrProgramLayout& programs;
  // An arm of an open cell, labelled `AC<state>_<action>`.
  bool conflicted(unsigned s, unsigned i) const {
    const auto row = a.action_row(s);
    return (i > 0 && row[i - 1].term == row[i].term) ||
           (i + 1 < row.size() && row[i + 1].term == row[i].term);
  }

  // A state entered by shifting the token in hand has that shift written
  // once, at `H`, ahead of every state.
  std::vector<int> hopElem{};
  std::vector<bool> laidInto{};
  bool shifted(unsigned s) const { return hopElem[s] != -2; }
  void plan_hops() {
    hopElem.assign(a.nstates, -2);
    laidInto.assign(a.nstates, false);
    for (unsigned st = 0; st < a.nstates; ++st)
      for (const LrAction& x : a.action_row(st)) {
        const auto into = [&](unsigned to) {
          const int e = static_cast<int>(a.cfg.terms[x.term].element);
          int& h = hopElem[to];
          if (h != -2 && h != e)
            throw std::logic_error(
                "LR: a state is shifted into by two elements");
          h = e;
          if (implied(x.term)) laidInto[to] = true;
        };
        if (x.kind == LrActionKind::shift) into(x.arg);
        if (x.kind == LrActionKind::guard)
          for (const std::uint16_t to : a.guards[x.arg].to)
            if (to != LrGuard::kNoState) into(to);
      }
  }
  // A probe's shift is written once per target and element, the token's
  // extent handed over in `hb` and `he`.
  using ProbeSet = std::set<std::pair<unsigned, unsigned>>;
  std::vector<std::vector<std::pair<unsigned, unsigned>>> probesOf{};
  void emit_probe_hop(unsigned s, const LrAction& x, std::string_view end,
                      std::string_view ind) {
    const unsigned elem = a.cfg.terms[x.term].element;
    probesOf[s].emplace_back(x.arg, elem);
    fmt("{0}hb = p;\n{0}he = {1};\n{0}goto P{2}_{3};\n", ind, end, x.arg, elem);
  }
  void emit_probe_hops(const ProbeSet& probes) {
    for (const auto& [to, elem] : probes)
      fmt("P{0}_{1}:\n  shift(c, {0}u, {1}, hb, he);\n  {2}\n", to, elem,
          jump('S', to));
  }
  // Where its group writes the probe's block for the same shift, `H` hands
  // the token in hand to it.
  void emit_hop(unsigned s, const ProbeSet& probes) {
    if (!shifted(s)) return;
    const auto elem = static_cast<unsigned>(hopElem[s]);
    if (laidInto[s])
      fmt("H{0}:\n  lay(c, {0}u, {1});\n  goto S{0};\n", s, elem);
    else if (probes.contains({s, elem}))
      fmt("H{0}:\n  hb = c.la.begin; he = c.la.end;\n  goto P{0}_{1};\n", s,
          elem);
    else
      fmt("H{0}:\n  shift(c, {0}u, {1}, c.la.begin, c.la.end);\n  goto S{0};\n",
          s, elem);
  }

  // The two-token oracle of one conflict cell, its sets spelled as literals.
  int peek_of(unsigned s, unsigned t) const {
    for (unsigned k = 0; k < a.npeeks; ++k)
      if (a.peeks[k].state == s && a.peeks[k].term == t)
        return static_cast<int>(k);
    return -1;
  }
  void emit_set_test(const char* var, const DynSet& set) {
    raw("(");
    bool first = true;
    for (unsigned t = 0; t < a.cfg.nterms; ++t) {
      if (!set.test(t)) continue;
      if (!first) raw(" || ");
      first = false;
      fmt("{} == {}", var, t);
    }
    if (first) raw("false");
    raw(")");
  }
  void emit_k2(unsigned pk, unsigned s, unsigned i) {
    const LrPeek& p = a.peeks[pk];
    line("      {");
    if (p.set[0].any())
      fmt("        const ::cursiv::SymbolId u0 = c.peekAt(c, {});\n",
          p.mode[0]);
    if (p.set[1].any() && p.set[0].any() && p.mode[1] == p.mode[0])
      line("        const ::cursiv::SymbolId u1 = u0;");
    else if (p.set[1].any())
      fmt("        const ::cursiv::SymbolId u1 = c.peekAt(c, {});\n",
          p.mode[1]);
    raw("        const bool in0 = ");
    emit_set_test("u0", p.set[0]);
    line(";");
    raw("        const bool in1 = ");
    emit_set_test("u1", p.set[1]);
    line(";");
    // Build-time disjointness proves the peek, so both build modes avoid
    // forking a resolver to confirm it.
    fmt("        if (in0 && !in1) goto AC{0}_{1};\n"
        "        if (in1 && !in0) goto AC{0}_{2};\n"
        "      }}\n",
        s, i, i + 1);
  }

  void emit_lex_tables() {
    if (anyPartialLadder()) {
      // A state's acceptance and boundary in one word, read together.
      array("[[maybe_unused]] inline constexpr ::std::uint32_t kMeta[]", a.ndfa,
            [&](std::size_t i) {
              num((std::uint32_t{a.dfaBound[i]} << 16) | a.dfaAccept[i]);
            });
      raw(R"(struct RowsTape : ::cursiv::LrLexTape<kTable> {
  ::std::uint16_t accept(::std::uint16_t s) const { return kMeta[s]; }
  bool bounded(::std::uint16_t s) const { return (kMeta[s] >> 16) != 0xFFFFu; }
  bool bound_has(::std::uint16_t s, const char* src, ::cursiv::TextOffset i,
                 ::cursiv::TextOffset len) const {
    return ::cursiv::lex_boundary(kTable.cfg).at(kMeta[s] >> 16, src, i, len);
  }
};
inline constexpr RowsTape kRowsTape{};
)");
    }
    line("inline constexpr ::cursiv::LrLexTape<kTable> kTape{};");
    if (a.nrelays != 0)
      raw(R"(using Relays = ::cursiv::LexRelayRun<::cursiv::LrLexTape<kTable>>;
inline ::std::uint16_t walked_to(const char* src, ::std::uint16_t mode,
                                 ::cursiv::TextOffset from,
                                 ::cursiv::TextOffset at) {
  ::std::uint16_t s = kTable.modeStart[mode];
  for (::cursiv::TextOffset i = from; i < at && s != 0xFFFFu; ++i)
    s = kTable.next_byte(s, static_cast<unsigned char>(src[i]));
  return s;
}
inline bool relay_among(::cursiv::TextOffset e, ::std::uint16_t t,
                        unsigned as, ::cursiv::TextOffset end,
                        ::std::uint16_t term, unsigned was) {
  if (e != end) return e > end;
  return t != 0xFFFFu && kTape.prefers_relayed(t, as, term, was);
}
inline bool relay_better(const char* src, ::cursiv::TextOffset len,
                         ::std::uint16_t mode, ::cursiv::TextOffset from,
                         ::cursiv::TextOffset e, ::std::uint16_t t,
                         unsigned as, ::cursiv::TextOffset end,
                         ::std::uint16_t term, ::cursiv::TextOffset& far) {
  if (e != end) return e > end;
  if (t == 0xFFFFu) return false;
  if (term == 0xFFFFu) return true;
  const ::std::uint16_t s = walked_to(src, mode, from, end);
  return kTape.prefers_relayed(
      t, as, term,
      s == 0xFFFFu ? 0u : kTape.tier(s, term, src, end, len, far));
}
inline bool relay_takes(const char* src, ::std::uint16_t mode,
                        ::cursiv::TextOffset from, ::cursiv::TextOffset at,
                        ::std::uint16_t reader, unsigned as,
                        ::std::uint16_t term) {
  if (term == 0xFFFFu || as == 0) return false;
  const ::std::uint16_t s = walked_to(src, mode, from, at);
  return s != 0xFFFFu && kTape.takes(s, kTape.taking(reader, as), term);
}
)");
    if (a.nrelays != 0 && reserving())
      raw(R"(inline bool relay_reserved(const char* src, ::cursiv::TextOffset len,
                           ::std::uint16_t mode, ::cursiv::TextOffset from,
                           ::cursiv::TextOffset at, ::std::uint16_t t,
                           ::cursiv::TextOffset& far) {
  const ::std::uint16_t s = walked_to(src, mode, from, at);
  return s != 0xFFFFu && kTape.reserves(s, t, src, from, at, len, far);
}
)");
    // Per terminal: trivia bit 0, recorded bit 1, a winner some mode flags bit
    // 2, joins bit 3.
    DynSet flagged;
    for (unsigned m = 0; m < a.nmodes; ++m) flagged |= a.modeWin[m];
    array("inline constexpr ::std::uint8_t kTermFlags[]", a.cfg.nterms,
          [&](std::size_t t) {
            num((a.cfg.terms[t].trivia ? 1u : 0u) |
                (a.cfg.terms[t].recorded ? 2u : 0u) |
                (flagged.test(static_cast<unsigned>(t)) ? 4u : 0u) |
                (a.cfg.terms[t].joins ? 8u : 0u));
          });
    if (any_recorded())
      array("inline constexpr ::std::uint16_t kTermElem[]", a.cfg.nterms,
            [&](std::size_t t) { num(a.cfg.terms[t].element); });
  }

  bool scalars() const { return a.cfg.wordRanges.size() != 0; }
  bool marked(unsigned set) const {
    return (scalars() || a.cfg.scalarRanges.size() != 0) &&
           a.cfg.setDecode[set] != kNoSet;
  }

  // Transposed, one row per target mode: the call site's `to` is a constant.
  void emit_mode_sub() {
    if (!subs) return;
    const unsigned rowWords = (a.nmodes + 63u) / 64u;
    fmt("inline constexpr ::std::uint64_t kModeSubTo[][{}] = {{", rowWords);
    for (unsigned to = 0; to < a.nmodes; ++to) {
      if (to) raw(",");
      raw("{");
      for (unsigned w = 0; w < rowWords; ++w) {
        std::uint64_t word = 0;
        for (unsigned from = w * 64u; from < a.nmodes && from < (w + 1u) * 64u;
             ++from)
          if (a.mode_covers(from, to)) word |= std::uint64_t{1} << (from & 63u);
        if (w) raw(",");
        fmt("{}ull", word);
      }
      raw("}");
    }
    raw(R"(};
[[maybe_unused]] inline bool mode_sub(unsigned from, unsigned to) {
  return ((kModeSubTo[to][from >> 6] >> (from & 63u)) & 1u) != 0;
}
)");
  }

  // The widest byte `lt8` answers for: past it the subtraction borrows out
  // of the lowest lane, the one `ctzll` reads.
  static constexpr unsigned kSwarBelowMax = 0x80;

  bool swarRuns = false, rangeRuns = false;
  static constexpr std::string_view kSwarMark = "\x01SWAR\n";

  void emit_swar() { raw(kSwarMark); }

  // Replaces the marker once every run is written: the forms they read are
  // not known before.
  void settle_swar() {
    std::string helpers =
        R"(CURSIV_ALWAYS_INLINE inline bool in_set(const ::std::uint64_t* s, unsigned char b) {
  return (s[b >> 6] >> (b & 63u)) & 1u;
}

)";
    if (swarRuns || rangeRuns)
      helpers +=
          R"(inline constexpr ::std::uint64_t kRep = 0x0101010101010101ull;
inline constexpr ::std::uint64_t kHi = 0x8080808080808080ull;
[[maybe_unused]] inline ::std::uint64_t ld64(const char* p) {
  ::std::uint64_t w;
  ::std::memcpy(&w, p, 8);
  if constexpr (::std::endian::native == ::std::endian::big) {
    w = ((w & 0x00ff00ff00ff00ffull) << 8) |
        ((w >> 8) & 0x00ff00ff00ff00ffull);
    w = ((w & 0x0000ffff0000ffffull) << 16) |
        ((w >> 16) & 0x0000ffff0000ffffull);
    w = (w << 32) | (w >> 32);
  }
  return w;
}

)";
    if (swarRuns)
      helpers +=
          R"(// `eq8` marks a byte equal to `b`, `lt8` a byte below it. ONLY THE LOWEST SET
// LANE IS EXACT: the borrow out of it can flag higher lanes that answer
// nothing, so a mask is read by `ctzll` alone -- a union of them keeps that,
// a negation or an intersection does not.
[[maybe_unused]] inline ::std::uint64_t eq8(::std::uint64_t w, unsigned char b) {
  const ::std::uint64_t x = w ^ (kRep * b);
  return (x - kRep) & ~x & kHi;
}

// ...and `lt8` answers no `b` above 0x80: `kRep * b` would set every lane's
// high bit and the subtraction borrow out of the lowest one, which is the
// lane read.
[[maybe_unused]] inline ::std::uint64_t lt8(::std::uint64_t w, unsigned char b) {
  return (w - kRep * b) & ~w & kHi;
}

)";
    if (rangeRuns)
      helpers +=
          R"(// The high bit of every lane holding lo <= x <= hi, exact: the lanes are
// seven bits wide, so neither sum carries into its neighbour.
[[maybe_unused]] inline ::std::uint64_t rng8(::std::uint64_t w, unsigned lo,
                                           unsigned hi, unsigned mask = 0x7f) {
  const ::std::uint64_t x = w & (kRep * mask);
  return (x + kRep * (0x80u - lo)) & ~(x + kRep * (0x7fu - hi)) & kHi;
}

)";
    out.replace(out.find(kSwarMark), kSwarMark.size(), helpers);
  }

  // The rows, read where a mode stops spelling states.
  void emit_rows_loop() {
    raw(R"(Wtab:
  {
)");
    if (a.nrelays == 0) line("    bool relays = false;");
    raw(R"(    ::cursiv::lex_walk_rows(kRowsTape, src, len, start, st, i, far, match,
        relays, [&](::std::uint16_t state, ::cursiv::TextOffset& at) {
          if (!::cursiv::lex_run_blind(kRowsTape, state)) return;
          while (at < len && kRowsTape.step(state,
                     static_cast<unsigned char>(src[at])) == state)
            ++at;
)");
    raw(R"(        });
  }
)");
  }

  // `case` labels several a line: the compiler folds a run of them as a range.
  template <class Each>
  void case_rows(Each&& each) {
    std::string row = "   ";
    each([&](unsigned v) {
      const std::string label = std::format(" case {}:", v);
      if (row.size() + label.size() > 80) {
        line(row);
        row = "   ";
      }
      row += label;
    });
    line(row);
  }
  void emit_moves() {
    // Wrappers over `lr_moves.hpp` with what this grammar knows at compile
    // time.
    const bool recorded = any_recorded();
    const std::string_view termElem = recorded ? "kTermElem" : "nullptr";
    line("inline void flush(::cursiv::LrContext& c) {");
    fmt("  ::cursiv::lr_flush<{}{}>(c, {});\n}}\n\n", recorded, stamp_more(),
        termElem);
    line("inline void records(::cursiv::LrContext& c) {");
    fmt("  ::cursiv::lr_record_trivia<{}{}>(c, {});\n}}\n\n", recorded,
        stamp_more(), termElem);
    raw(R"(CURSIV_ALWAYS_INLINE inline void shift(::cursiv::LrContext& c,
                                       ::std::uint32_t tgt, ::std::uint16_t row,
                                       ::cursiv::TextOffset b,
                                       ::cursiv::TextOffset e) {
)");
    line("  records(c);");
    fmt("  ::cursiv::lr_push_leaf{}(c, tgt, ::cursiv::ElementId(row), b, e);\n",
        stamp_args());
    // The gap starts where the last token with a width ended.
    if (conds()) line("  if (e != b) c.virt.from = e;");
    line("}");
    line("");
    // An implied token is laid where the last token ended, and the trivia
    // past it is read again behind it. A dedent is shifted: the trivia of its
    // suite's last line is the suite's.
    if (!impliedTerms.empty()) {
      fmt(R"(inline void lay(::cursiv::LrContext& c, ::std::uint32_t tgt,
                ::std::uint16_t row) {{
  ::cursiv::lr_push_leaf{}(c, tgt, ::cursiv::ElementId(row), c.committed,
                       c.committed);
)",
          stamp_args());
      if (recorded) line("  c.pending.clear();");
      line("}");
      line("");
    }
  }

  // A parse probes the fold table on nearly every reduce: the multiplier that
  // scatters these keys best is searched, a tie keeping the first.
  std::uint32_t fused_mul(unsigned slots) const {
    static constexpr unsigned kTries = 16384;
    const unsigned shift = cursiv::lr_fused_shift(slots), mask = slots - 1;
    std::vector<std::uint32_t> keys(a.nfused);
    for (unsigned j = 0; j < a.nfused; ++j)
      keys[j] = cursiv::lr_fused_key(a.fused[j].node, a.fused[j].site);
    std::vector<std::uint8_t> marks(slots, 0);
    std::uint8_t* const stamp = marks.data();
    const std::uint32_t* const kp = keys.data();
    const std::size_t n = keys.size();
    std::uint8_t gen = 0;
    // The probes past each key's first; a try that reaches `cap` has lost.
    const auto spill = [&](std::uint32_t mul, unsigned cap) {
      if (++gen == 0) {
        std::ranges::fill(marks, std::uint8_t{0});
        gen = 1;
      }
      unsigned extra = 0;
      const auto place = [&](std::uint32_t k) {
        unsigned h = (k * mul) >> shift;
        while (stamp[h] == gen) {
          h = (h + 1) & mask;
          ++extra;
        }
        stamp[h] = gen;
      };
      std::size_t j = 0;
      for (; j + 4 <= n; j += 4) {
        place(kp[j]);
        place(kp[j + 1]);
        place(kp[j + 2]);
        place(kp[j + 3]);
        if (extra >= cap) return cap;
      }
      for (; j < n; ++j) place(kp[j]);
      return std::min(extra, cap);
    };
    std::uint32_t best = 0x9E3779B1u;
    unsigned bestExtra = spill(best, ~0u);
    std::uint32_t x = best;
    for (unsigned i = 0; i < kTries && bestExtra != 0; ++i) {
      x = x * 1664525u + 1013904223u;
      const std::uint32_t m = x | 1u;
      if (const unsigned v = spill(m, bestExtra); v < bestExtra) {
        bestExtra = v;
        best = m;
      }
    }
    return best;
  }

  void emit_folds() {
    if (a.nfused == 0) {
      raw(R"(inline ::cursiv::ElementId folds(::cursiv::ElementId,
                                  ::cursiv::ElementId) {
  return ::cursiv::kNoElement;
}

)");
      return;
    }
    const unsigned slots = cursiv::lr_fused_slots(a.nfused);
    const std::uint32_t mul = fused_mul(slots);
    std::vector<std::uint32_t> fk(slots, 0);
    std::vector<std::uint16_t> fv(slots, 0);
    cursiv::lr_fused_hash(a.fused.data(), a.nfused, slots, mul, fk.data(),
                          fv.data());
    array("inline constexpr ::std::uint32_t kFusedKey[]", slots,
          [&](std::size_t i) { num(fk[i]); });
    array("inline constexpr ::std::uint16_t kFusedVal[]", slots,
          [&](std::size_t i) { num(fv[i]); });
    fmt("inline constexpr ::cursiv::ElementId kFusedFirst = {};\n"
        "inline constexpr ::std::uint32_t kFusedMul = {}u;\n"
        "inline ::cursiv::ElementId folds(::cursiv::ElementId held, "
        "::cursiv::ElementId over)"
        " {{\n"
        "  return ::cursiv::lr_fused_probe(kFusedKey, kFusedVal, kFusedMul,"
        " {}u, {}u,"
        " kFusedFirst, held, over);\n}}\n\n",
        a.writes.nchains, mul, cursiv::lr_fused_shift(slots), slots - 1u);
  }

  void emit_data() {
    emit_lex_tables();
    emit_mode_sub();
    emit_moves();
    emit_folds();
    if (a.multiCand) {
      emit_multi_data();
      emit_cands_at();
      emit_pick_cand();
    }
  }

  bool candidate_blind(unsigned st) const {
    if (a.dfaRelay[st] != kNoRelay || a.dfa_res_set(st).any()) return false;
    for (unsigned t = 1; t < a.cfg.nterms; ++t)
      if (a.dfa_acc_set(st).test(t) && a.dfa_acc_bound(st, t) != kNoSet)
        return false;
    return true;
  }

  struct CandidatePolicy {
    LrParserEmitter& emit;

    void emit_match(unsigned st, std::string_view ind) {
      emit.fmt("{}match.take(kTape, {}u, src, i, len, far);\n", ind, st);
    }
    void emit_relay_call(unsigned st, std::string_view ind) {
      emit.emit_relay_call(st, ind);
    }
    bool blind(unsigned st) const { return emit.candidate_blind(st); }
    bool plain_run(unsigned) const { return false; }
    int continuation_of(unsigned, const std::uint8_t*) const { return -1; }
    void emit_bound_exit(unsigned, const std::uint8_t*) {}
    void emit_run_adv(const ByteSet& stay, std::string_view var,
                      std::string_view set) {
      emit.emit_run_adv(stay, var, set);
    }
  };

  void emit_candidate_rows(const std::vector<unsigned>& starts) {
    std::vector<std::uint8_t> used(a.ndfa, 0);
    for (const unsigned start : starts)
      for (const unsigned st : dfa.states_from(start)) used[st] = 1;
    std::map<std::array<std::uint64_t, 4>, std::vector<unsigned>> runs;
    for (unsigned st = 0; st < a.ndfa; ++st) {
      if (!used[st] || !candidate_blind(st)) continue;
      const ByteSet stay = dfa.stay(st);
      if (stay.any())
        runs[{stay.w[0], stay.w[1], stay.w[2], stay.w[3]}].push_back(st);
    }
    raw(R"(  ::cursiv::lex_walk_rows(kTape, src, len, start, st, i, far, match, relays,
      [&](::std::uint16_t state, ::cursiv::TextOffset& at) {
)");
    if (!runs.empty()) {
      line("    switch (state) {");
      for (const auto& [words, states] : runs) {
        for (const unsigned st : states) fmt("    case {}:\n", st);
        line("    {");
        ByteSet stay{};
        std::copy(words.begin(), words.end(), stay.w);
        raw("      static constexpr ::std::uint64_t kStay[4] = ");
        set64(stay);
        line(";");
        const auto shape = classify_run(stay);
        if (shape.swar || shape.ranges)
          line(
              "      if (at >= len || !in_set(kStay, "
              "static_cast<unsigned char>(src[at]))) break;");
        emit_run_adv(stay, "at", "kStay");
        line("      break;");
        line("    }");
      }
      line("    default: break;");
      line("    }");
    } else {
      line("        (void)state;");
      line("        (void)at;");
    }
    raw(R"(      });
)");
  }

  void emit_candidate_walk() {
    std::vector<unsigned> starts;
    bool partial = false;
    for (unsigned m = 0; m < a.nmodes; ++m) {
      if (!sweepMode(m)) continue;
      const unsigned start = a.modeStart[m];
      if (std::ranges::find(starts, start) != starts.end()) continue;
      starts.push_back(start);
      partial |= dfa.ladder_from(start).size() != dfa.states_from(start).size();
    }
    raw(R"(inline void match_all(const char* src, ::cursiv::TextOffset len,
                      ::cursiv::TextOffset i, ::cursiv::Scan* v,
                      ::std::uint32_t& n, ::std::uint32_t cap,
                      ::std::uint16_t st) {
  const ::cursiv::TextOffset start = i;
  const ::std::uint16_t first = st;
  ::cursiv::TextOffset far = i;
  unsigned char b;
  (void)b;
  ::cursiv::LrLexCandidates<kTable, kCandBase, kCandPool, kCandBound, kMaxCands>
)");
    line(refusing() ? "      match(v, n, cap, start);" : "      match(v, n, cap);");
    line(a.nrelays != 0 ? "  Relays relays;" : "  bool relays = false;");
    if (partial) {
      // Large collectors keep the table loop to limit dispatch pressure.
      emit_candidate_rows(starts);
    } else {
      line("  switch (st) {");
      for (const unsigned start : starts)
        fmt("    case {0}: goto W{0};\n", start);
      line("    default: return;");
      line("  }");
      std::vector<unsigned> order;
      std::vector<std::uint8_t> spelled(a.ndfa, 0);
      for (const unsigned start : starts)
        for (const unsigned st : dfa.ladder_from(start))
          if (!spelled[st]) {
            spelled[st] = 1;
            order.push_back(st);
          }
      CandidatePolicy policy{*this};
      for (const unsigned st : order) dfa.state(*this, policy, st);
      line("Wdone:");
    }
    raw(R"(  match.finish(src, len, start, first, relays);
}

)");
  }

  bool knownCandidates = false;
  std::vector<std::uint8_t> candidateClean{};
  bool emit_known_candidates() {
    if (refusing() || a.nrelays != 0) return false;
    std::vector<std::vector<unsigned>> before(a.ndfa);
    std::vector<unsigned> work;
    candidateClean.assign(a.ndfa, 1);
    auto& clean = candidateClean;
    for (unsigned st = 0; st < a.ndfa; ++st) {
      for (unsigned k = 0; k < a.ncls; ++k) {
        const auto to = a.next_dfa(st, k);
        if (to != kDeadState) before[to].push_back(st);
      }
      if (!candidate_blind(st)) {
        clean[st] = 0;
        work.push_back(st);
      }
    }
    // No suffix can withdraw candidates or hide a later acceptance.
    while (!work.empty()) {
      const auto st = work.back();
      work.pop_back();
      for (const auto from : before[st])
        if (clean[from]) {
          clean[from] = 0;
          work.push_back(from);
        }
    }
    std::vector<unsigned> ends;
    std::vector<std::uint8_t> seen(a.ndfa, 0);
    for (unsigned m = 0; m < a.nmodes; ++m) {
      if (!multiMode(m) || a.modeWin[m].none()) continue;
      const auto st = a.modeStart[m];
      if (a.dfa_acc_set(st).any()) continue;
      for (unsigned k = 0; k < a.ncls; ++k) {
        const auto to = a.next_dfa(st, k);
        if (to == kDeadState || seen[to] || !clean[to] ||
            a.dfaAccept[to] == 0 || !a.modeWin[m].test(a.dfaAccept[to] - 1u) ||
            a.dfa_acc_set(to).rank(a.cfg.nterms) > 2)
          continue;
        seen[to] = 1;
        ends.push_back(to);
      }
    }
    if (ends.empty()) return false;
    raw(R"(inline void match_after(const char* src, ::cursiv::TextOffset len,
                        ::cursiv::TextOffset from, ::cursiv::Scan best,
                        ::cursiv::Scan* v, ::std::uint32_t& n,
                        ::std::uint32_t cap, ::std::uint16_t first) {
  if (best.end == from + 1u && kCandBase[first] == kCandBase[first + 1u]) {
    const auto st = kTable.next_byte(first, static_cast<unsigned char>(src[from]));
    switch (st) {
)");
    case_rows([&](auto&& put) {
      for (const auto st : ends) put(st);
    });
    raw(std::format(
        R"(      for (unsigned k = kCandBase[st]; k < kCandBase[st + 1u]; ++k) {{
        assert(n < cap);
        if (n < cap) v[n++] = ::cursiv::Scan{{best.end, kCandPool[k]{}}};
      }}
      return;
    default: goto full;
    }}
  }}
full:
  n = 0;
  match_all(src, len, from, v, n, cap, first);
}}

)",
        a.tiered() ? ", kTable.cold.accTier[k]" : ""));
    return true;
  }

  void emit_multi_data() {
    fmt("// Terminals one position of this grammar can match at once, as the\n"
        "// table counted them: the room every reading of a position fills.\n"
        "inline constexpr ::std::uint32_t kMaxCands = {}u;\n",
        a.maxCands != 0 ? a.maxCands : 1);
    line("// The candidate walk's own data: what each DFA state of a flagged");
    line("// mode accepts and reserves, and the word class.");
    line("inline constexpr const auto& kCandBase = kTable.cold.accBoundBase;");
    line("inline constexpr const auto& kCandBound = kTable.cold.accBound;");
    std::vector<unsigned> cands;
    for (unsigned st = 0; st < a.ndfa; ++st) {
      // `kCandBase` counts every bit a state accepts.
      assert(!a.dfa_acc_set(st).test(0));
      for (unsigned t = 1; t < a.cfg.nterms; ++t)
        if (a.dfa_acc_set(st).test(t)) cands.push_back(t);
    }
    array(
        "inline constexpr ::std::uint16_t kCandPool[]", cands.size(),
        [&](std::size_t k) { num(cands[k]); }, "0", true);
    emit_candidate_walk();
    knownCandidates = emit_known_candidates();
  }

  // One entry over the modes that read several candidates.
  void emit_cands_at() {
    line("inline void cands_at(const char* src, ::cursiv::TextOffset len,");
    line("                     ::std::uint16_t mode, ::cursiv::TextOffset i,");
    line("                     ::cursiv::Scan* v, ::std::uint32_t& n,");
    line("                     ::std::uint32_t cap) {");
    line("  switch (mode) {");
    for (unsigned m = 0; m < a.nmodes; ++m) {
      if (!sweepMode(m)) continue;
      fmt("    case {}: match_all(src, len, i, v, n, cap, {}u); break;\n", m,
          a.modeStart[m]);
    }
    line("    default: break;");
    line("  }");
    line("}");
    line("");
  }

  void emit_pick_cand() {
    raw(R"(// Several terminals match at the cursor, and THE RESERVE IS THE
// STATE'S: what it has no action for is no reading of these bytes, and
// what it alone admits it takes. Where it admits several, the same
// look-ahead that decides an open cell decides here -- the candidates
// are the arms, ranked as the deterministic matcher would have answered.
inline void rank_cands(::cursiv::Scan* v, ::std::uint32_t n) {
  // They share one extent -- the sweep answers no other -- so the longest
  // match has already spoken: the rank the walk elects by decides.
  const auto before = [](const ::cursiv::Scan& x, const ::cursiv::Scan& y) {
    return ::cursiv::lex_term_before(
        ::cursiv::lex_rank(x.tier, (kTermFlags[x.term] & 1u) != 0), x.term,
        ::cursiv::lex_rank(y.tier, (kTermFlags[y.term] & 1u) != 0), y.term);
  };
  for (::std::uint32_t k = 1; k < n; ++k)
    for (::std::uint32_t j = k; j > 0 && before(v[j], v[j - 1]); --j)
      ::std::swap(v[j], v[j - 1]);
}

inline void pick_cand(::cursiv::LrContext& c, ::std::uint32_t s) {
  // The sweep read this position; whatever it left is what stands here, and
  // the token the walk elected may not be among it.
  const ::cursiv::Scan best = c.cand[0];
  ::std::uint32_t n = 0;
  for (::std::uint32_t k = 0; k < c.ncand; ++k)
    if (kTable.cold.follow[s].test(c.cand[k].term)) c.cand[n++] = c.cand[k];
  c.ncand = 0;  // consumed: the same position is never read twice
  if (n <= 1) {
    const ::cursiv::Scan one = n == 0 ? best : c.cand[0];
    c.la = ::cursiv::Token{c.la.begin, one.end, one.term, c.laMode};
    return;
  }
  rank_cands(c.cand, n);
  // Held apart from `c.cand`: deciding reads ahead, and a reading of its own
  // fills that room with the candidates of another position.
  assert(n <= kMaxCands);
  ::cursiv::ScanRoom<kMaxCands> held;
  ::cursiv::Scan* const room = held.data();
  for (::std::uint32_t k = 0; k < n; ++k) room[k] = c.cand[k];
  const ::std::uint32_t k = c.candAt(c, s, room, n);
  // Past every candidate: the graph reads the position, and the state stops
  // on a token it has no action for.
  if (k >= n) [[unlikely]] {
    c.la = ::cursiv::Token{c.la.begin, c.la.begin, ::cursiv::kInvalidSymbol,
                           c.laMode};
    return;
  }
  c.la = ::cursiv::Token{c.la.begin, room[k].end, room[k].term, c.laMode};
}

)");
  }

  // The two eight-byte shapes of a run: everything from `lo` up but at most
  // four bytes, or a union of a few ASCII ranges. Either step may stop early,
  // never late: the byte loop after it decides the end.
  struct RunShape {
    bool swar = false;
    unsigned char lo = 0;
    unsigned char b[4]{};
    unsigned n = 0;
    bool ranges = false;
    unsigned char from[4]{};
    unsigned char to[4]{};
    unsigned nr = 0;
    bool high = false;  // every byte past 0x7F is a member

    bool paired(unsigned k) const {
      if ((from[k] >> 5) != (to[k] >> 5)) return false;
      for (unsigned j = 0; j < nr; ++j)
        if (from[j] == (from[k] ^ 0x20u) && to[j] == (to[k] ^ 0x20u))
          return true;
      return false;
    }
  };

  static RunShape classify_run(const ByteSet& stay) {
    RunShape r;
    // Only a content-shaped run pays for the eight-byte step.
    const unsigned count = stay.count();
    if (count <= 4) return r;
    // Below lo every byte must be absent; above it at most four are absent.
    // Removing lo bytes from the complement gives its first possible bound.
    const unsigned lo = count < 252 ? 252 - count : 0;
    if (lo <= kSwarBelowMax) {
      bool below = false;
      for (unsigned x = 0; x < lo; ++x)
        below = below || stay.has(static_cast<unsigned char>(x));
      if (!below) {
        r.swar = true;
        r.lo = static_cast<unsigned char>(lo);
        for (unsigned x = lo; x < 256; ++x)
          if (!stay.has(static_cast<unsigned char>(x)))
            r.b[r.n++] = static_cast<unsigned char>(x);
        return r;
      }
    }
    unsigned nr = 0;
    for (unsigned x = 0; x < 0x80; ++x) {
      if (!stay.has(static_cast<unsigned char>(x))) continue;
      if (nr != 0 && r.to[nr - 1] + 1u == x) {
        r.to[nr - 1] = static_cast<unsigned char>(x);
        continue;
      }
      if (nr == 4) return r;
      r.from[nr] = r.to[nr] = static_cast<unsigned char>(x);
      ++nr;
    }
    if (nr == 0) return r;
    // The ranges are read off the low seven bits, so the high half is a
    // member whole or not at all.
    unsigned high = 0;
    for (unsigned x = 0x80; x < 256; ++x)
      if (stay.has(static_cast<unsigned char>(x))) ++high;
    if (high != 0 && high != 0x80) return r;
    r.ranges = true;
    r.nr = nr;
    r.high = high != 0;
    return r;
  }

  unsigned runId = 0;

  // Advance `var` over the run of `stay` bytes, eight at a time where the set
  // has a SWAR shape.
  void emit_run_adv(const ByteSet& stay, std::string_view var,
                    std::string_view setName, bool content = true) {
    RunShape r = classify_run(stay);
    if (!content) r.ranges = false;
    if (!r.swar && !r.ranges) {
      fmt("    while ({0} < len && in_set({1}, "
          "static_cast<unsigned char>(src[{0}]))) ++{0};\n",
          var, setName);
      return;
    }
    (r.swar ? swarRuns : rangeRuns) = true;
    const unsigned id = runId++;
    fmt("    while (len - {0} >= 8) {{\n"
        "      const ::std::uint64_t w = ld64(src + {0});\n",
        var);
    if (r.swar) {
      assert(r.lo <= kSwarBelowMax && "lt8 answers no byte above it");
      fmt("      const ::std::uint64_t bad = lt8(w, {})", r.lo);
      for (unsigned k = 0; k < r.n; ++k) fmt(" | eq8(w, {})", r.b[k]);
    } else {
      raw("      const ::std::uint64_t bad = (~(");
      std::string_view sep;
      for (unsigned k = 0; k < r.nr; ++k) {
        const bool folded = r.paired(k);
        if (folded && (r.from[k] & 0x20u)) continue;
        fmt("{}rng8(w, {}, {}{})", sep, r.from[k], r.to[k],
            folded ? ", 0x5f" : "");
        sep = " | ";
      }
      raw(r.high ? ") & kHi & ~w)" : ") & kHi) | (w & kHi)");
    }
    fmt(";\n"
        "      if (bad) {{ {0} += static_cast<::cursiv::TextOffset>("
        "::std::countr_zero(bad) >> 3); goto Lrun{1}; }}\n"
        "      {0} += 8;\n"
        "    }}\n"
        "    while ({0} < len && in_set({2}, "
        "static_cast<unsigned char>(src[{0}]))) \n++{0};\n"
        "Lrun{1}:;\n",
        var, id, setName);
  }

  void emit_relay_runs() {
    if (a.nrelays == 0) return;
    raw("::cursiv::TextOffset Table::relay_run(::std::uint16_t state, const "
        "char* src,\n"
        "    ::cursiv::TextOffset len, ::cursiv::TextOffset i) {\n"
        "  (void)src;\n  (void)len;\n  switch (state) {\n");
    std::vector<std::uint8_t> used(a.ndfa, 0);
    for (unsigned k = 0; k < a.nrelays; ++k)
      for (const unsigned st : dfa.states_from(a.relays[k].resume))
        used[st] = 1;
    for (unsigned st = 0; st < a.ndfa; ++st) {
      if (!used[st] || !blind(st)) continue;
      const ByteSet stay = dfa.stay(st);
      if (!stay.any()) continue;
      fmt("    case {}u: {{\n", st);
      fmt("    static constexpr ::std::uint64_t kStay[] = "
          "{{{}ull,{}ull,{}ull,{}ull}};\n",
          stay.w[0], stay.w[1], stay.w[2], stay.w[3]);
      emit_run_adv(stay, "i", "kStay");
      raw("    return i;\n    }\n");
    }
    raw("    default: return i;\n  }\n}\n");
  }

  // Whether a state also accepts a terminal that stands only where no byte
  // follows: its token is read there with the others, not alone.
  bool ends_the_text(unsigned st) const {
    for (unsigned t = 1; t < a.cfg.nterms; ++t) {
      if (!a.dfa_acc_set(st).test(t)) continue;
      const auto b = a.dfa_acc_bound(st, t);
      if (lex_bound_ends(a.cfg, b)) return true;
    }
    return false;
  }

  // Only an unguarded one-byte literal with no continuation bypasses the lexer.
  bool byte_literal(unsigned m, unsigned t) const {
    const std::string_view sp = a.cfg.term_text(t);
    if (sp.size() != 1 || !a.cfg.terms[t].spelled ||
        a.cfg.termBuild[t].folded || a.cfg.terms[t].refuses)
      return false;
    const auto st =
        a.next_dfa(a.modeStart[m], a.cls[static_cast<unsigned char>(sp[0])]);
    if (st == kDeadState || a.dfaAccept[st] != t + 1u ||
        a.dfaRelay[st] != kNoRelay || a.dfaBound[st] != kNoSet ||
        ends_the_text(st))
      return false;
    for (unsigned k = 0; k < a.ncls; ++k)
      if (a.next_dfa(st, k) != kDeadState) return false;
    return true;
  }

  mutable std::unordered_map<unsigned, int> exclusiveTerm{};
  mutable std::unordered_map<unsigned, std::vector<ByteSet>> exclusiveBytes{};

  int exclusive_term(unsigned e) const {
    const auto [at, fresh] = exclusiveTerm.try_emplace(e, -1);
    if (!fresh) return at->second;
    DynSet out;
    for (const auto st : dfa.states_from(e)) {
      // A guard can hand the match to an acceptance the ranked one hid.
      out |= a.dfa_acc_set(st);
      for (auto r = a.dfaRelay[st]; r != kNoRelay; r = a.relays[r].next)
        out.set(a.relays[r].term);
      if (out.more_than_one()) return -1;
    }
    for (unsigned t = 0; t < a.cfg.nterms; ++t)
      if (out.test(t)) return at->second = static_cast<int>(t);
    return -1;
  }

  ByteSet exclusive_bytes(unsigned m, unsigned t) const {
    const std::uint16_t start = a.modeStart[m];
    const auto [at, fresh] = exclusiveBytes.try_emplace(start);
    if (fresh) {
      at->second.resize(a.cfg.nterms);
      for (unsigned b = 0; b < 256; ++b) {
        const auto e = a.next_dfa(start, a.cls[b]);
        if (e == kDeadState || e == start) continue;
        const int term = exclusive_term(e);
        if (term >= 0) at->second[term].add(static_cast<unsigned char>(b));
      }
    }
    return at->second[t];
  }

  bool walk_partial(unsigned m) const {
    return dfa.ladder_from(a.modeStart[m]).size() !=
           dfa.states_from(a.modeStart[m]).size();
  }

  void emit_match(unsigned st, std::string_view ind) {
    if (a.dfaAccept[st] != 0)
      fmt("{}match.take(kTape, {}u, src, i, len, far);\n", ind, st);
  }

  bool plain_run(unsigned st) const {
    if (a.dfaAccept[st] == 0 ||
        a.dfaBound[st] != kNoSet || a.dfaRelay[st] != kNoRelay)
      return false;
    bool self = false;
    for (unsigned k = 0; k < a.ncls; ++k) {
      const auto to = a.next_dfa(st, k);
      if (to != st && to != kDeadState) return false;
      self = self || to == st;
    }
    return self;
  }

  int continuation_of(unsigned st, const std::uint8_t* spelled) const {
    if (a.dfaAccept[st] == 0 ||
        a.dfaBound[st] != kNoSet || a.dfaRelay[st] != kNoRelay)
      return -1;
    std::vector<unsigned> tried;
    for (unsigned k = 0; k < a.ncls; ++k) {
      const auto to = a.next_dfa(st, k);
      if (to == st || to == kDeadState || (spelled && !spelled[to]) ||
          std::find(tried.begin(), tried.end(), to) != tried.end())
        continue;
      tried.push_back(to);
      if (a.dfaAccept[to] != a.dfaAccept[st] || !plain_run(to)) continue;
      unsigned common = 0, exceptions = 0;
      bool same = true;
      for (unsigned b = 0; b < 256; ++b) {
        const auto edge = a.next_dfa(st, a.cls[b]);
        const auto run = a.next_dfa(to, a.cls[b]);
        if ((edge == kDeadState) != (run == kDeadState)) {
          same = false;
          break;
        }
        common += edge == to;
        exceptions += edge != kDeadState && edge != to;
      }
      if (same && common > 1 && exceptions <= 4) return to;
    }
    return -1;
  }

  void emit_bound_exit(unsigned st, const std::uint8_t* spelled) {
    const auto bound = a.dfaBound[st];
    if (a.dfaAccept[st] == 0 || bound == kNoSet ||
        a.dfaRelay[st] != kNoRelay || marked(bound))
      return;
    unsigned to = kDeadState;
    for (unsigned b = 0; b < 256; ++b) {
      const auto edge = a.next_dfa(st, a.cls[b]);
      if ((edge != kDeadState) != a.cfg.sets[bound].has(b)) return;
      if (edge == kDeadState) continue;
      if (to != kDeadState && to != edge) return;
      to = edge;
    }
    if (to == kDeadState || (spelled && !spelled[to]) ||
        a.dfaAccept[to] == 0 || a.dfaBound[to] != kNoSet ||
        a.dfaRelay[to] != kNoRelay)
      return;
    fmt("  if (i < len && ::cursiv::lex_boundary(kTable.cfg).at({}, src, i, "
        "len)) "
        "{{ ++i; goto W{}; }}\n",
        bound, to);
    line("  end = i;");
    fmt("  term = {};\n", a.dfaAccept[st] - 1u);
    line("  goto Wdone;");
  }

  bool blind(unsigned st) const {
    return lex_run_blind(a.dfaAccept[st] != 0, a.dfaBound[st] != kNoSet,
                         a.dfaRelay[st] != kNoRelay);
  }

  void walk_head() {
    raw(R"(  const ::cursiv::TextOffset start = i;
  ::cursiv::TextOffset far = i;
  ::cursiv::LexMatch match{i, 0xFFFFu};
  auto& end = match.end;
  auto& term = match.term;
  unsigned char b;
  (void)src;
  (void)len;
  (void)b;
  (void)start;
  (void)far;
)");
    if (a.nrelays != 0) line("  Relays relays;");
  }

  // The relays launched at a state, to compete with the walk's own match.
  void emit_relay_call(unsigned st, std::string_view ind) {
    for (auto k = a.dfaRelay[st]; k != kNoRelay; k = a.relays[k].next)
      fmt("{}relays.launch(kTape, {}u, src, len, start, i);\n", ind, k);
  }

  void walk_foot() {
    line("Wdone:");
    if (a.nrelays != 0) {
      raw(R"(  if (relays.any()) {
    ::cursiv::TextOffset relayEnd = start;
    ::std::uint16_t relayTerm = 0xFFFFu;
    unsigned relayAs = 0;
    relays.finish(kTape, src, len, far,
                  [&](::std::uint16_t t, ::cursiv::TextOffset e, unsigned as) {
)");
      if (reserving())
        raw(R"(                    if (e == end && term != 0xFFFFu &&
                        relay_reserved(src, len, mode, start, end, t, far))
                      return;
)");
      raw(R"(                    if (e == relayEnd && t == relayTerm) {
                      relayAs = as > relayAs ? as : relayAs;
                    } else if (relay_among(e, t, as, relayEnd, relayTerm,
                                           relayAs)) {
                      relayEnd = e;
                      relayTerm = t;
                      relayAs = as;
                    }
                  });
)");
      // A relayed token is asked the refusals of a walked one.
      const char* const wins =
          "(relayEnd == end &&\n        relay_takes(src, mode, start, "
          "end, relayTerm, relayAs, term)) ||\n       relay_better(src, len, "
          "mode, start, relayEnd, relayTerm, relayAs, end, term, far)";
      raw(refusing() ? std::format(R"(    if ({}) {{
      end = relayEnd;
      term = relayTerm;
    }}
  }}
)",
                                   wins)
                     : std::format(R"(    if ({})
      return ::cursiv::Scan{{relayEnd, relayTerm}};
  }}
)",
                                   wins));
    }
    line(refusing()
             ? "  return ::cursiv::lr_unrefused_hot<kTable>(src, len, "
               "kTable.modeStart[mode], start, ::cursiv::Scan{end, term});"
             : "  return ::cursiv::Scan{end, term};");
    line("}");
    line("");
  }
  bool refusing() const {
    for (unsigned t = 0; t < a.cfg.nterms; ++t)
      if (a.cfg.terms[t].refuses) return true;
    return false;
  }
  bool reserving() const {
    for (unsigned s = 0; s < a.ndfa; ++s)
      if (a.dfa_res_set(static_cast<std::uint16_t>(s)).any()) return true;
    return false;
  }

  void emit_walk() {
    const std::span<const std::uint16_t> starts{a.modeStart.data(), a.nmodes};
    std::vector<unsigned> order;
    std::vector<std::uint8_t> spelled(a.ndfa, 0);
    bool partial = false;
    for (const auto start : starts) {
      const auto& mine = dfa.ladder_from(start);
      partial = partial || mine.size() != dfa.states_from(start).size();
      for (const unsigned st : mine)
        if (!spelled[st]) {
          spelled[st] = 1;
          order.push_back(st);
        }
    }
    line(
        "inline ::cursiv::Scan walk_all(const char* src, ::cursiv::TextOffset "
        "len,");
    line("         ::std::uint16_t mode, ::cursiv::TextOffset i) {");
    walk_head();
    if (partial) line("  ::std::uint16_t st = 0;");
    raw("  static const void* const kWEntry[] = {");
    for (unsigned m = 0; m < starts.size(); ++m) {
      if (m) raw(",");
      if (m % 8 == 0) raw("\n      ");
      fmt("&&W{}", starts[m]);
    }
    line("};");
    line("  goto *kWEntry[mode];");
    for (const unsigned st : order)
      dfa.state(*this, *this, st, partial ? spelled.data() : nullptr);
    if (partial) emit_rows_loop();
    walk_foot();
  }

  // A clean start reaches only unconditional acceptances, so the sweep elects
  // the same token without changing the candidates' insertion order.
  bool collect_first(unsigned m) const {
    if (!multiMode(m) || !a.modeWin[m].more_than_one() ||
        candidateClean.empty() || !candidateClean[a.modeStart[m]] ||
        a.dfa_acc_set(a.modeStart[m]).any())
      return false;
    for (const unsigned st : dfa.states_from(a.modeStart[m])) {
      for (unsigned t = 1; t < a.cfg.nterms; ++t) {
        if (!a.dfa_acc_set(st).test(t)) continue;
        const auto& term = a.cfg.terms[t];
        if (!term.trivia && !term.spelled && !a.modeWin[m].test(t))
          return false;
      }
    }
    return true;
  }

  std::vector<unsigned> wsLead{};
  // Modes alike in blanks, candidate collection and the winners swept behind
  // share one tail, which takes the mode as an argument.
  std::vector<unsigned> tailLead{};
  void plan_lexers(const std::string& used) {
    wsLead.resize(a.nmodes);
    tailLead.resize(a.nmodes);
    std::map<std::array<std::uint64_t, 4>, unsigned> first;
    std::map<std::tuple<unsigned, unsigned, bool, std::string>, unsigned> tails;
    for (unsigned m = 0; m < a.nmodes; ++m) {
      wsLead[m] = m;
      tailLead[m] = m;
      if (!used[m]) continue;
      ByteSet ws{};
      mode_ws(m, ws);
      wsLead[m] =
          first.try_emplace(std::array{ws.w[0], ws.w[1], ws.w[2], ws.w[3]}, m)
              .first->second;
      tailLead[m] = tails
                        .try_emplace(std::tuple{a.modeMulti[m], wsLead[m],
                                                collect_first(m), win_test(m)},
                                     m)
                        .first->second;
    }
  }

  void emit_lexer(unsigned m) {
    if (ByteSet ws{}; mode_ws(m, ws) && wsLead[m] == m) {
      fmt("inline constexpr ::std::uint64_t kWs_{}[4] = ", m);
      set64(ws);
      line(";");
    }
    if (tailLead[m] == m) emit_lex_tail(m);
    fmt("inline void lex_{}(::cursiv::LrContext& c) {{\n", m);
    if (!impliedTerms.empty())
      fmt("  if (c.virt.pending != 0) {{ c.la = c.virt.in_hand(); "
          "c.laMode = {}; return; }}\n",
          m);
    line("  c.pending.clear();");
    line("  c.pendEnd = c.committed;");
    if (indented())
      fmt("  if (c.indent.depth == 0 && ::cursiv::indent_virtual_at(c.src, "
          "c.len, "
          "c.indentArena, c.indent.head, {}, {}, c.committed, c.la, "
          "[&c] {{ return c.tape.held(); }})) {{\n"
          "{}    c.laMode = {};\n    return;\n  }}\n",
          indentTerm, dedentTerm, multiMode(m) ? "    c.ncand = 0;\n" : "", m);
    fmt("  lex_tail_{}(c, c.committed, {});\n", tailLead[m], m);
    if (a.modeLonger[m] != kNoMode) line("  lengthen(c);");
    if (flags(m))
      fmt("  if (kTable.flagged({0}, c.la.sym)) [[unlikely]] "
          "::cursiv::lr_drive_exact<kTable>(c, {0});\n",
          m);
    line("}");
    line("");
  }

  void emit_lengthen() {
    raw(R"(// The token in hand is the longer of the mode's own and the one it lexes
// for length alone, the trivia before it crossed.
inline void lengthen(::cursiv::LrContext& c) {
  const ::cursiv::Scan own{c.la.end, c.la.sym};
  const ::cursiv::Scan out = ::cursiv::lex_longest(
      ::cursiv::LrLexTape<kTable>{}, c.la.mode, own, [&](::std::uint16_t more) {
        return walk_all(c.src, c.len, more, c.la.begin);
      });
  if (out.end != own.end) {
    c.la = ::cursiv::Token{c.la.begin, out.end, out.term, c.la.mode};
    c.ncand = 0;
  }
}

)");
  }

  // What keeps a token another mode lexed from standing here: it hides a rival
  // this mode flags -- for a mode of the second kind, where it ends the text.
  std::string owes_walk(unsigned m) const {
    const std::string win = win_test(m, "c.la.sym");
    if (win.empty()) return "";
    return eofMode(m) ? " && !(c.la.end == c.len && (" + win + "))"
                      : " && !(" + win + ")";
  }

  // Whether the token in hand is a winner the mode flags as hiding a rival,
  // spelled per mode; empty where it flags none.
  std::string win_test(unsigned m, std::string_view term = "s.term") const {
    std::string out;
    const DynSet& win = a.modeWin[m];
    for (unsigned w = 0; w * 64u < a.cfg.nterms; ++w) {
      const std::uint64_t mask = win.word(w);
      if (mask == 0) continue;
      if (!out.empty()) out += " || ";
      if (std::has_single_bit(mask))
        out += std::format("{} == {}u", term, w * 64u + std::countr_zero(mask));
      else
        out += std::format(
            "(({0} >> 6) == {1}u && (0x{2:x}ull >> ({0} & 63u)) & 1u)", term, w,
            mask);
    }
    return out;
  }

  // Never inlined: a copy per lexing state makes the file compile twice as
  // slowly.
  void emit_lex_tail(unsigned m) {
    fmt("CURSIV_NOINLINE static void lex_tail_{}(::cursiv::LrContext& c, "
        "::cursiv::TextOffset p, ::std::uint16_t mode) {{",
        m);
    constexpr std::string_view mode = "mode",
                               start = "kTable.modeStart[mode]";
    raw(R"(
  const char* const src = c.src;
  const ::cursiv::TextOffset len = c.len;
again:
)");
    // The candidates belong to the position the sweep read them at: a
    // trivia that moved `p` clears them here.
    line("  if (p >= len) [[unlikely]] {");
    if (multiMode(m)) line("    c.ncand = 0;");
    line("    c.la = ::cursiv::Token{p, p, 0};");
    line("    goto done;");
    line("  }");
    if (ByteSet ws{}; mode_ws(m, ws)) {
      const std::string nm = std::format("kWs_{}", wsLead[m]);
      fmt("  {{\n    if (in_set({}, "
          "static_cast<unsigned char>(src[p]))) {{\n",
          nm);
      line("      ++p;");
      emit_run_adv(ws, "p", nm, false);
      raw(R"(      c.pendEnd = p;
      goto again;
    }
  }
)");
    }
    const bool asks = multiMode(m);
    const bool collected = collect_first(m);
    const std::string win = asks || eofMode(m) ? win_test(m) : std::string();
    const std::string flagged =
        a.modeWin[m].more_than_one()
            ? "(kTermFlags[s.term] & 4u) && (" + win + ")"
            : win;
    if (collected) {
      line("  {\n    c.ncand = 0;");
      fmt("    match_all(src, len, p, c.cand, c.ncand, kMaxCands, {});\n",
          start);
      raw(R"(    if (c.ncand == 0u) {
      c.la = ::cursiv::Token{p, p, 0xFFFFu};
      goto done;
    }
    ::cursiv::Scan s = c.cand[0];
    for (::std::uint32_t k = 1; k < c.ncand; ++k)
      if (kTape.prefers(c.cand[k].term, c.cand[k].tier, s.term, s.tier))
        s = c.cand[k];
)");
      fmt("    if (!({})) c.ncand = 0;\n", win);
    } else {
      raw(asks ? "  {\n    ::cursiv::Scan s = "
               : "  {\n    const ::cursiv::Scan s = ");
      fmt("walk_all(src, len, {}, p);\n"
          "    if (s.term == 0xFFFFu || s.end == p) {{\n",
          mode);
      // Where the ranked acceptance's boundary refuses, the terminals it
      // outranked are still there, and only the sweep holds them.
      if (asks)
        fmt("      c.ncand = 0;\n"
            "      match_all(src, len, p, c.cand, c.ncand, kMaxCands, {0});\n"
            "      rank_cands(c.cand, c.ncand);\n"
            "      if (c.ncand == 0u) {{\n"
            "        c.la = ::cursiv::Token{{p, p, 0xFFFFu}};\n"
            "        goto done;\n"
            "      }}\n"
            "      s = c.cand[0];\n"
            "      if ((kTermFlags[s.term] & {1}u) == 0) {{\n"
            "        c.la = ::cursiv::Token{{p, s.end, s.term}};\n"
            "        {2}c.ncand = 0;\n"
            "        goto done;\n"
            "      }}\n",
            start, indented() ? 9 : 1,
            win.empty() ? "" : "if (!(" + win + ")) ");
      else
        raw("      c.la = ::cursiv::Token{p, p, 0xFFFFu};\n      goto done;\n");
      line("    }");
    }
    raw(R"(    if (kTermFlags[s.term] & 1u) {
)");
    if (any_recorded())
      raw(R"(      if (kTermFlags[s.term] & 2u)
        c.pending.push_back(::cursiv::Token{p, s.end, s.term});
)");
    raw(R"(      p = s.end;
      c.pendEnd = p;
      goto again;
    }
)");
    if (indented())
      raw(R"(    if (c.indent.depth != 0 && (kTermFlags[s.term] & 8u)) {
      p = s.end;
      c.pendEnd = p;
      goto again;
    }
)");
    raw(R"(    c.la = ::cursiv::Token{p, s.end, s.term};
)");
    if (asks && !collected) {
      line("    c.ncand = 0;");
      if (!win.empty())
        fmt("    if ({})\n"
            "      {}(src, len, p, {}c.cand, c.ncand, kMaxCands, {});\n",
            flagged, knownCandidates ? "match_after" : "match_all",
            knownCandidates ? "s, " : "", start);
    } else if (eofMode(m)) {
      // The rivals meet where the text ends, and the state reading the token
      // is the one on top of the stack.
      fmt("    if (s.end == len && ({})) "
          "[[unlikely]] {{\n"
          "      c.laMode = {};\n"
          "      c.ncand = 0;\n"
          "      match_all(src, len, p, c.cand, c.ncand, kMaxCands, {});\n"
          "      pick_cand(c, c.stack.top().state);\n"
          "    }}\n",
          flagged, mode, start);
    }
    line("  }");
    line("done:");
    fmt("  c.laMode = {};\n", mode);
    fmt("  c.la.mode = {};\n", mode);
    line("}");
    line("");
  }

  // Equivalent production plans share one reduction entry. The common move
  // implementation specializes on the first production's constant index.
  std::vector<unsigned> reduceOf{};
  void plan_reduces(const std::string& used) {
    plan_settles();
    reduceOf.resize(a.cfg.nprods);
    std::map<std::vector<unsigned>, unsigned> seen;
    for (unsigned prod = 0; prod < a.cfg.nprods; ++prod) {
      reduceOf[prod] = prod;
      if (!used[prod]) continue;
      const auto& p = a.cfg.prods[prod];
      const auto& plan = a.reductions[prod];
      std::vector<unsigned> key{p.lhs,        p.node,
                                p.len,        static_cast<unsigned>(plan.form),
                                plan.flushes, plan.turnRow};
      for (unsigned k = 0; k < plan.count; ++k) {
        const auto& n = a.reduceNames[plan.names + k];
        key.insert(key.end(), {n.slot, n.row, n.fold});
      }
      reduceOf[prod] = seen.try_emplace(std::move(key), prod).first->second;
    }
  }

  std::uint32_t goto_guard(unsigned from, unsigned lhs) const {
    for (const auto e : a.gotoRows[from])
      if (e.nt == lhs) return e.guard;
    return 0;
  }
  std::string guard_target(std::uint32_t guard,
                           std::string_view first = "") const {
    return std::format(
        "::cursiv::lr_guard_land<kTable>({}u, [&](unsigned k) {{ "
        "return {}; }})",
        guard - 1u,
        first.empty() ? "drive_cond(c, k, c.virt.from, c.la)"
                      : std::format("::cursiv::lr_held<kTable>({}, k)", first));
  }

  std::string reduce_allowed(unsigned prod) const {
    return std::format("reduce_allowed(c, {}u)", prod);
  }

  std::vector<bool> landed{};
  void emit_guarded_landings(std::uint32_t guard, std::string_view ind,
                             const auto& landing, std::string_view none,
                             std::string_view first = "") {
    const auto& targets = a.guards[guard - 1u].to;
    landed.resize(a.nstates);
    fmt("{}switch ({}) {{\n", ind, guard_target(guard, first));
    for (const std::uint16_t to : targets) {
      if (to == LrGuard::kNoState || landed[to]) continue;
      landed[to] = true;
      fmt("{}case {}u:\n", ind, to);
      landing(to);
    }
    for (const std::uint16_t to : targets)
      if (to != LrGuard::kNoState) landed[to] = false;
    fmt("{}default: {}\n{}}}\n", ind, none, ind);
  }

  // Where a closed node is still owed the state it settles in: a goto column
  // outside this set never settles anything.
  void plan_settles() {
    std::vector<bool> live(a.nstates, false);
    std::vector<bool> spreadNt(a.cfg.nnts, false);
    std::vector<unsigned> work;
    const auto spread = [&](unsigned lhs) {
      if (spreadNt[lhs]) return;
      spreadNt[lhs] = true;
      const auto reach = [&](unsigned to) {
        if (reads_a_token(to) || live[to]) return;
        live[to] = true;
        work.push_back(to);
      };
      for (const auto e : gotos[lhs]) {
        reach(e.to);
        if (e.guard != 0)
          for (const std::uint16_t to : a.guards[e.guard - 1u].to)
            if (to != LrGuard::kNoState) reach(to);
      }
    };
    for (unsigned q = 0; q < a.cfg.nprods; ++q)
      if (a.cfg.prods[q].node != kNoRow) spread(a.cfg.prods[q].lhs);
    settlesPlain.assign(a.cfg.nnts, false);
    while (!work.empty()) {
      const unsigned st = work.back();
      work.pop_back();
      for (const LrAction& act : a.action_row(st)) {
        if (act.kind != LrActionKind::reduce) continue;
        const auto& q = a.cfg.prods[act.arg];
        spread(q.lhs);
        if (q.node == kNoRow) settlesPlain[q.lhs] = true;
      }
    }
  }
  std::vector<bool> settlesPlain{};
  [[nodiscard]] bool settles(unsigned lhs, bool node) const {
    return node || settlesPlain[lhs];
  }

  // A close waiting on a cascade is answered by the first state that can
  // read a token.
  [[nodiscard]] bool reads_a_token(unsigned s) const {
    return a.readsToken[s] != 0;
  }

  void emit_tail(unsigned prod) {
    const auto& p = a.cfg.prods[prod];
    const int one = lone_goto(p.lhs);
    const std::string none = leave("error");
    const char* const row = a.reductions[prod].row(p.len) == LrReduceRow::none
                                ? "::cursiv::kNoElement"
                                : "rec";
    const auto land = [&](unsigned to) {
      fmt("      ::cursiv::lr_reduce_finish<kTable.stamp>(c, {}u, {}, {});\n",
          to, row, settles(p.lhs, p.node != kNoRow) && reads_a_token(to));
      fmt("      {}\n", jump('G', to));
    };
    if (one >= 0) {
      land(static_cast<unsigned>(one));
      return;
    }
    line("      switch (c.stack[c.stack.size() - 2u].state) {");
    struct Landing {
      std::uint16_t to;
      std::uint32_t guard;
      std::vector<std::uint16_t> from;
    };
    std::vector<Landing> landings;
    std::map<std::pair<std::uint16_t, std::uint32_t>, std::size_t> at;
    for (const auto e : gotos[p.lhs]) {
      const auto [it, fresh] =
          at.try_emplace(std::pair{e.to, e.guard}, landings.size());
      if (fresh) landings.push_back({e.to, e.guard, {}});
      landings[it->second].from.push_back(e.from);
    }
    for (const Landing& l : landings) {
      for (const std::uint16_t from : l.from) fmt("      case {}:\n", from);
      if (l.guard == 0)
        land(l.to);
      else
        emit_guarded_landings(l.guard, "        ", land, none,
                              p.len == 0 ? "" : "c.stack.top().state");
    }
    fmt("      default: {}\n      }}\n", none);
  }

  // The landing switches that two reductions of one group spell alike, each
  // written once under a `T` label. Below this many lines a switch costs less
  // repeated than the jump costs on every reduction.
  static constexpr unsigned kLrTailLines = 32;
  using TailKey = std::pair<std::string_view, LrReduceRow>;
  std::vector<Piece> tailPiece{};
  void render_tails(const std::string& used) {
    tailPiece.assign(a.cfg.nprods, {});
    for (unsigned prod = 0; prod < a.cfg.nprods; ++prod)
      if (used[prod] && reduceOf[prod] == prod)
        tailPiece[prod] = piece([&] { emit_tail(prod); });
  }

  void emit_reduce(unsigned prod, std::string_view tail,
                   std::optional<unsigned> shared) {
    fmt("R{}:\n", prod);
    line("    {");
    fmt("      {}::cursiv::lr_reduce_frame<kTable>(c, "
        "::std::integral_constant<::std::uint16_t, {}>{{}}, folds);\n",
        a.reductions[prod].row(a.cfg.prods[prod].len) == LrReduceRow::none
            ? ""
            : "rec = ",
        prod);
    if (shared)
      fmt("      goto T{};\n", *shared);
    else
      raw(tail);
    line("    }");
  }

  bool mode_ws(unsigned m, ByteSet& ws) const {
    for (unsigned k = 0; k < 4; ++k) ws.w[k] = a.skipSet[std::size_t{m} * 4 + k];
    return ws.any();
  }

  // The entry of a state that does not probe: no shift enters it, its mode
  // collects candidates in its lexer, or its grammar reads indentation first.
  void emit_plain_lex(unsigned s) {
    const unsigned m = a.mode[s];
    fmt("  if (c.laMode != {}) {{\n", m);
    if (subs) {
      fmt("    if (c.laMode != ::cursiv::kNoMode && mode_sub(c.laMode, {}){}) "
          "goto D{};\n",
          m, owes_walk(m), s);
    }
    fmt("    lex_{}(c);\n  }}\n", m);
    if (shifted(s) || shares(s)) {
      fmt("  goto C{};\n", s);
      emit_entry_labels(s);
      fmt("  lex_{}(c);\n", m);
      fmt("C{}:\n", s);
    }
    if (multiMode(m))
      fmt("  if (c.ncand != 0) [[unlikely]] pick_cand(c, {});\n", s);
  }

  // The arm owed by the labels written since it was set.
  std::string pendBody{};

  // Terminals reading the same arm, consecutive ids as a range.
  unsigned caseLo = 0, caseHi = 0;
  bool caseOpen = false;
  void case_label(unsigned t) {
    if (caseOpen && t == caseHi + 1) {
      caseHi = t;
      return;
    }
    flush_cases();
    caseLo = caseHi = t;
    caseOpen = true;
  }
  void flush_cases() {
    if (!caseOpen) return;
    caseOpen = false;
    case_rows([&](auto&& put) {
      for (unsigned t = caseLo; t <= caseHi; ++t) put(t);
    });
  }

  void emit_dispatch(unsigned s) {
    const unsigned m = a.mode[s];
    pendBody.clear();
    caseOpen = false;
    fmt("D{}:\n", s);
    curState = s;
    const unsigned base = a.actionBase[s], count = a.actionCount[s];
    const LrAction* const row = a.actions.data() + base;
    std::vector<std::size_t> cellLead(count);
    std::vector<std::vector<unsigned>> cellTerms(count);
    std::map<std::vector<std::uint16_t>, std::size_t> reductions;
    lr_each_run(row, count, [&](std::size_t i, std::size_t j) {
      cellLead[i] = i;
      if (j - i > 1 && peek_of(s, row[i].term) < 0) {
        std::vector<std::uint16_t> key;
        for (std::size_t k = i; k < j && row[k].kind == LrActionKind::reduce;
             ++k)
          key.push_back(row[k].arg);
        // Reductions read the lookahead from the context, not the row's term.
        if (key.size() == j - i)
          cellLead[i] = reductions.try_emplace(std::move(key), i).first->second;
      }
      if (j - i > 1) cellTerms[cellLead[i]].push_back(row[i].term);
    });
    if (dedentTerm != 0 && acts_on(s, dedentTerm))
      fmt("  if (c.la.sym == 0 && c.indent.head != ::cursiv::kNoIndent) {{ "
          "c.la = ::cursiv::Token{{c.la.begin, c.la.begin, {}}}; }}\n",
          dedentTerm);
    line("  switch (c.la.sym) {");
    lr_each_run(row, count, [&](std::size_t i, std::size_t j) {
      const LrAction& x = row[i];
      const std::size_t run = j - i;
      if (cellLead[i] != i) return;
      // Terminals that answer the same way share their labels: gcc and clang
      // do not merge identical arms alike.
      if (run == 1) {
        std::string body = capture([&] { emit_arm(x); });
        if (body != pendBody) {
          flush_cases();
          raw(pendBody);
          pendBody = std::move(body);
        }
        case_label(x.term);
        return;
      }
      flush_cases();
      raw(pendBody);
      pendBody.clear();
      for (unsigned t : cellTerms[i]) case_label(t);
      flush_cases();
      const bool peeked = run == 2 && peek_of(s, x.term) >= 0;
      if (peeked)
        emit_k2(static_cast<unsigned>(peek_of(s, x.term)), s,
                static_cast<unsigned>(i));
      // An arm that is one jump, into this group or out of it, is jumped to
      // from its case. The peek names every arm's label.
      std::vector<std::string> bodies;
      std::vector<std::string> jumps;
      for (std::size_t k = i; k < j; ++k) {
        const std::string& b =
            bodies.emplace_back(capture([&] { emit_arm(row[k]); }));
        const bool one =
            !peeked &&
            (b.starts_with("    goto ") || b.starts_with("    @J")) &&
            b.find('\n') == b.size() - 1;
        jumps.push_back(one ? b.substr(4, b.size() - 5) : std::string{});
      }
      const LrProgramRows<std::uint32_t, std::uint64_t> matrix{
          programs.actions.blocks, programs.actions.values, a.cfg.nterms, 0};
      const auto cell = matrix.get(s, x.term);
      fmt("      switch (c.resolveAt(c, c.stack.top().state, {}u, {}u)) {{\n",
          cell >> programs.countBits, run);
      for (std::size_t k = i; k < j; ++k)
        fmt("      case {}u: {}\n", k - i,
            jumps[k - i].empty() ? std::format("goto AC{}_{};", s, k)
                                 : jumps[k - i]);
      fmt("      default: {}\n", leave("error"));
      line("      }");
      for (std::size_t k = i; k < j; ++k) {
        if (!jumps[k - i].empty()) continue;
        fmt("    AC{}_{}:\n", s, k);
        raw(bodies[k - i]);
      }
    });
    flush_cases();
    raw(pendBody);
    pendBody.clear();
    line("    default:");
    if (guards(s)) fmt("    X{}:\n", s);
    if (subs) {
      // A subsumed lookahead that missed re-lexes once.
      fmt("      if (c.laMode != {}) {{ lex_{}(c); {}goto D{}; }}\n", m, m,
          multiMode(m) ? "if (c.ncand != 0) [[unlikely]] pick_cand(c, "
                         "c.stack.top().state); "
                       : "",
          s);
    }
    // An end of input refused while suites are open owes a dedent first.
    if (dedentTerm != 0)
      fmt("      if (c.la.sym == 0 && "
          "c.indent.head != ::cursiv::kNoIndent) {{ "
          "c.la = ::cursiv::Token{{c.la.begin, c.la.begin, {}}}; "
          "goto D{}; }}\n",
          dedentTerm, s);
    // A refused token is an implied one where the state answers it and its
    // condition holds, once per position.
    for (unsigned q = 0; q < impliedTerms.size(); ++q)
      if (acts_on(s, impliedTerms[q]))
        fmt("      if (imply_{}(c)) goto D{};\n", q, s);
    // No token at all, in a tight mode, still answers the one reduce a
    // condition guards, where its goto lands; anywhere else it is an error.
    if (const auto sole = a.soleReduce[s];
        sole != kLrNoProd && a.cfg.prods[sole].gap != 0 &&
        a.modeTight[a.mode[s]] != 0)
      fmt("      if (c.la.sym == 0xFFFFu && {}{}) {}\n",
          reduce_allowed(sole),
          may_land_nowhere(s, sole)
              ? std::format(" && {} != ::cursiv::kInvalidSymbol",
                            guard_target(goto_guard(s, a.cfg.prods[sole].lhs)))
              : std::string{},
          jump('R', reduceOf[sole]));
    fmt("      {}\n  }}\n}}\n", leave("pause"));
  }

  // Lex and dispatch in one: a closed single byte needs no walk, and a
  // terminal with exclusive entry bytes needs no dispatch after its walk.
  void emit_entry_probe(unsigned s) {
    const unsigned m = a.mode[s];
    struct Probe {
      LrAction action;
      ByteSet bytes;
      bool literal;
    };
    std::vector<Probe> probes;
    ByteSet taken{};
    bool cut = false;
    for (unsigned i = 0; i < a.actionCount[s]; ++i) {
      const auto& x = a.action(a.actionBase[s] + i);
      if (conflicted(s, i) || x.kind == LrActionKind::guard ||
          (x.kind == LrActionKind::reduce && a.cfg.prods[x.arg].gap != 0) ||
          !byte_literal(m, x.term))
        continue;
      if (probes.size() == kLrProbeLits) {
        cut = true;
        break;
      }
      ByteSet bytes{};
      bytes.add(static_cast<unsigned char>(a.cfg.term_text(x.term)[0]));
      taken |= bytes;
      probes.push_back({x, bytes, true});
    }
    const auto literals = probes.size();
    for (unsigned i = 0; i < a.actionCount[s]; ++i) {
      const auto& x = a.action(a.actionBase[s] + i);
      if (x.kind != LrActionKind::shift || conflicted(s, i) ||
          byte_literal(m, x.term))
        continue;
      const ByteSet bytes = exclusive_bytes(m, x.term).andnot(taken);
      if (!bytes.any()) continue;
      if (probes.size() - literals == kLrProbeNamed) {
        cut = true;
        break;
      }
      taken |= bytes;
      probes.push_back({x, bytes, false});
    }
    if (cut) ++probeCuts;
    fmt("  if (c.laMode != ::cursiv::kNoMode) {{ if (c.laMode == {}) goto D{}",
        m, s);
    if (subs)
      fmt("; if (mode_sub(c.laMode, {}){}) goto D{}", m, owes_walk(m), s);
    fmt("; lex_{}(c); goto D{}; }}\n", m, s);
    emit_entry_labels(s);
    line("  {");
    line("    ::cursiv::TextOffset p = c.committed;");
    if (ByteSet ws{}; mode_ws(m, ws))
      emit_run_adv(ws, "p", std::format("kWs_{}", wsLead[m]), false);
    line("    c.pendEnd = p;");
    fmt("    if (p >= len) {{ c.la = ::cursiv::Token{{p, p, 0, {0}}};"
        " c.laMode = {0}; goto D{1}; }}\n",
        m, s);
    if (!probes.empty()) {
      line("    switch (static_cast<unsigned char>(src[p])) {");
      for (const auto& probe : probes) {
        const auto& x = probe.action;
        case_rows([&](auto&& put) {
          for (unsigned b = 0; b < 256; ++b)
            if (probe.bytes.has(static_cast<unsigned char>(b))) put(b);
        });
        line("    {");
        if (probe.literal) {
          if (x.kind == LrActionKind::shift) {
            emit_probe_hop(s, x, "p + 1", "      ");
          } else {
            assert(x.kind == LrActionKind::reduce);
            fmt("      c.la = ::cursiv::Token{{p, p + 1, {}, {}}};\n"
                "      c.laMode = {};\n",
                x.term, m, m);
            emit_empty_landing(s, x.arg, "      ");
            fmt("      {}\n", jump('R', reduceOf[x.arg]));
          }
        } else {
          fmt("      const ::cursiv::Scan sc = walk_all(src, len, {}, p);\n",
              m);
          fmt("      if (sc.term == {} && sc.end > p) [[likely]] {{\n", x.term);
          emit_probe_hop(s, x, "sc.end", "        ");
          line("      }");
          fmt("      c.la = ::cursiv::Token{{p, p, 0xFFFFu, {}}};\n", m);
          fmt("      c.laMode = {};\n", m);
          fmt("      goto D{};\n", s);
        }
        line("    }");
      }
      line("    default: break;");
      line("    }");
    }
    fmt("    lex_tail_{}(c, p, {});\n", tailLead[m], m);
    if (flags(m))
      fmt("    if (kTable.flagged({0}, c.la.sym)) [[unlikely]] "
          "::cursiv::lr_drive_exact<kTable>(c, {0});\n",
          m);
    line("  }");
  }

  // One action, as a state writes it.
  void emit_land(unsigned to, std::string_view ind) {
    assert(shifted(to));
    fmt("{}{}\n", ind, jump('H', to));
  }
  void emit_take(const LrAction& x, std::string_view ind) {
    if (implied(x.term)) fmt("{}c.virt.pending = 0;\n", ind);
    if (indentTerm != 0 && x.term == indentTerm)
      fmt("{}c.indent.head = ::cursiv::indent_push(c.indentArena, "
          "c.indent.head, c.la.begin, c.la.end - c.la.begin, false);\n",
          ind);
    if (dedentTerm != 0 && x.term == dedentTerm)
      fmt("{}c.indent.head = ::cursiv::indent_pop(c.indentArena, "
          "c.indent.head);\n",
          ind);
    if (indented() && a.cfg.terms[x.term].delim > 0)
      fmt("{}++c.indent.depth;\n", ind);
    if (indented() && a.cfg.terms[x.term].delim < 0)
      fmt("{}if (c.indent.depth != 0) --c.indent.depth;\n", ind);
  }
  // An empty rule's reduce whose goto the gap decides and may leave nowhere:
  // there the token has no action in state `s`.
  bool may_land_nowhere(unsigned s, unsigned prod) const {
    const auto& q = a.cfg.prods[prod];
    if (q.len != 0) return false;
    const std::uint32_t k = goto_guard(s, q.lhs);
    return k != 0 &&
           std::ranges::count(a.guards[k - 1u].to, LrGuard::kNoState) != 0;
  }
  void emit_empty_landing(unsigned s, unsigned prod, std::string_view ind) {
    if (!may_land_nowhere(s, prod)) return;
    fmt("{}if ({} == ::cursiv::kInvalidSymbol) goto X{};\n", ind,
        guard_target(goto_guard(s, a.cfg.prods[prod].lhs)), s);
  }
  void emit_arm(const LrAction& x) {
    if (lr_takes_token(x.kind)) {
      if (x.kind == LrActionKind::shift) {
        emit_take(x, "    ");
        emit_land(x.arg, "    ");
        return;
      }
      // Nothing moves before an outcome lands: one that lands nowhere leaves
      // the token without an action.
      emit_guarded_landings(
          x.arg + 1u, "    ",
          [&](unsigned to) {
            emit_take(x, "      ");
            emit_land(to, "      ");
          },
          std::format("goto X{};", curState));
      return;
    }
    if (x.kind == LrActionKind::reduce) {
      // Where the reduce's condition fails, the token has no action here.
      if (const unsigned g = a.cfg.prods[x.arg].gap; g != 0)
        fmt("    if (!{}) goto X{};\n", reduce_allowed(x.arg), curState);
      emit_empty_landing(curState, x.arg, "    ");
      fmt("    {}\n", jump('R', reduceOf[x.arg]));
    } else {
      line("    goto Acc;");
    }
  }

  // The tokens the parse lays: the indentation's, and the implied ones.
  unsigned indentTerm = 0, dedentTerm = 0;
  std::vector<unsigned> impliedTerms{};
  void plan_laid() {
    for (unsigned t = 1; t < a.cfg.nterms; ++t) {
      const auto laid = a.cfg.terms[t].laid;
      if (laid == kLaidIndent) indentTerm = t;
      if (laid == kLaidDedent) dedentTerm = t;
      if (laid == kLaidImplied) impliedTerms.push_back(t);
      bracketed = bracketed || a.cfg.terms[t].delim != 0;
    }
    subs = any_sub() && !indented();
  }
  bool indented() const {
    return indentTerm != 0 || dedentTerm != 0 || bracketed;
  }
  bool bracketed = false;
  unsigned curState = 0;
  bool implied(unsigned term) const {
    return a.cfg.terms[term].laid == kLaidImplied;
  }
  bool guards(unsigned s) const {
    for (const LrAction& x : a.action_row(s)) {
      if (x.kind == LrActionKind::guard ||
          (x.kind == LrActionKind::reduce &&
           (a.cfg.prods[x.arg].gap != 0 || may_land_nowhere(s, x.arg))))
        return true;
    }
    return false;
  }
  bool conds() const { return a.cfg.ngapConds != 0; }

  GapEmitter<LrBuildCfg> gapEmitter{a.cfg, "kTable.cfg.sets.data()",
                                    "::cursiv::lex_boundary(kTable.cfg)"};
  PredicateEmitter<LrBuildCfg> predicateEmitter{a.cfg, gapEmitter,
                                                a.sectionErr};
  GapReach<LrBuildCfg> gapReach{a.cfg};
  NamedHead<LrBuildCfg> namedHead{a.cfg};

  std::string gap_leaf(unsigned l, bool partial = false) const {
    const GapLeaf& leaf = a.cfg.gapLeaves[l];
    std::string expression;
    switch (leaf.kind) {
      case GapLeafKind::has:
      case GapLeafKind::is:
        // A long gap is read once for every reading that asks it.
        expression = std::format(
            "::cursiv::lr_gap_ask({}, {}u, from, n, [&] {{ return {}; }})",
            partial ? "gaps" : "&c.gaps", l, gapEmitter.expression(l));
        break;
      case GapLeafKind::end:
        expression = partial ? "(known && ::cursiv::gap_end(text.size(), la))"
                             : "::cursiv::gap_end(c.len, la)";
        break;
      case GapLeafKind::cut:
        expression = gap_cut_expression(a.cfg, leaf, "la.sym");
        break;
      case GapLeafKind::head:
      case GapLeafKind::phrase:
        expression = std::format(
            "predicate_{}({}, la, from)", l,
            partial
                ? "text.data(), static_cast<::cursiv::TextOffset>(text.size())"
                : "c.src, c.len");
        if (partial)
          return "(known ? 2u * " + expression + " : " +
                 (leaf.kind == GapLeafKind::head && named_heads(l)
                      ? std::format("named_{}(la.sym)", l)
                      : std::string("1u")) +
                 ")";
        break;
    }
    return partial ? "(2u * " + expression + ")" : expression;
  }
  bool named_heads(unsigned l) const {
    return namedHead.any(a.cfg.gapLeaves[l].node);
  }
  void emit_named_heads() {
    for (unsigned l = 0; l < a.cfg.ngapLeaves; ++l)
      if (a.cfg.gapLeaves[l].kind == GapLeafKind::head && named_heads(l))
        raw(namedHead.source("static", std::format("named_{}", l),
                             a.cfg.gapLeaves[l].node));
  }
  void emit_caret_seen() {
    if (!gapReach.skips_any()) return;
    fmt("::cursiv::TextOffset caret_seen(::std::string_view text, unsigned k,\n"
        "                                ::cursiv::TextOffset from,\n"
        "                                const ::cursiv::Token& {}) {{\n",
        tight_conds().empty() ? "la" : "in");
    emit_tight_token();
    raw("  ::cursiv::TextOffset seen = 0;\n"
        "  const auto len = static_cast<::cursiv::TextOffset>(text.size());\n"
        "  (void)len;\n  switch (k) {\n");
    for (unsigned k = 0; k < a.cfg.ngapConds; ++k) {
      if (!gapReach.skips_any(k)) continue;
      fmt("    case {}:\n", k);
      for (const std::uint16_t v : gapReach.leaves(k))
        if (gapReach.skips(v))
          fmt("      predicate_{}(text.data(), len, la, from, &seen);\n", v);
      raw("      break;\n");
    }
    raw("    default:\n      break;\n  }\n  return seen;\n}\n\n");
  }
  std::string gap_cond(unsigned k, bool partial = false) const {
    return GapConditionEmitter(a.cfg).source(
        k, [&](unsigned leaf) { return gap_leaf(leaf, partial); }, partial);
  }

  // A condition read tight sees an empty gap: the token in hand is set aside
  // for an empty one where the gap begins.
  std::string tight_conds() const {
    std::string e;
    for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
      if (k < a.cfg.gapTight.size() && a.cfg.gapTight[k] != 0)
        e += std::format("{}k == {}u", e.empty() ? "" : " || ", k);
    return e;
  }
  void emit_tight_token() {
    const std::string t = tight_conds();
    if (t.empty()) return;
    fmt("  const ::cursiv::Token la = {} ? ::cursiv::Token{{from, from, 0xFFFFu}}"
        " : in;\n",
        t);
  }
  void emit_predicate_data() {
    if (!conds()) return;
    raw(predicateEmitter.declarations());
  }

  void emit_drive_cond() {
    if (!conds()) return;
    const std::string_view la = tight_conds().empty() ? "la" : "in";
    // The caret's reading answers false, unknown or true.
    const auto body = [&](bool caret) {
      emit_tight_token();
      fmt("  const char* const g = {} + from;\n",
          caret ? "text.data()" : "c.src");
      raw("  const ::cursiv::TextOffset n = la.begin > from ? la.begin - from "
          ": 0;\n  (void)g;\n  (void)n;\n");
      if (caret) line("  (void)known;\n  (void)gaps;");
      line("  switch (k) {");
      for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
        if (caret)
          fmt("    case {}: return ({}) != 0;\n", k, gap_cond(k, true));
        else
          fmt("    case {}: return {};\n", k, gap_cond(k));
      raw("  }\n  return false;\n}\n\n");
    };
    fmt("bool drive_cond(const ::cursiv::LrContext& c, unsigned k,\n"
        "                ::cursiv::TextOffset from, const ::cursiv::Token& {}) "
        "{{\n",
        la);
    body(false);
    if (std::ranges::any_of(std::span(a.cfg.prods.data(), a.cfg.nprods),
                            [](const auto& p) { return p.gap != 0; }))
      raw(R"(CURSIV_ALWAYS_INLINE static inline bool reduce_allowed(
    const ::cursiv::LrContext& c, ::std::uint16_t prod) {
  return ::cursiv::lr_reduce_allowed<kTable>(prod, [&](unsigned k) {
    return drive_cond(c, k, c.virt.from, c.la);
  });
}

)");
    emit_named_heads();
    fmt("bool caret_cond(::std::string_view text, unsigned k,\n"
        "                ::cursiv::TextOffset from, const ::cursiv::Token& {}, "
        "bool known,\n"
        "                ::cursiv::LrGapMemo* gaps) {{\n",
        la);
    body(true);
    emit_caret_seen();
  }
  // One helper per implied token, out of line: the refused path.
  void emit_imply() {
    for (unsigned q = 0; q < impliedTerms.size(); ++q)
      fmt("CURSIV_NOINLINE static bool imply_{0}(::cursiv::LrContext& c) {{\n"
          "  if (c.virt.pending != 0 || (c.virt.laid_at(c.la) & {3}u) != 0 ||\n"
          "      !drive_cond(c, {1}u, c.virt.from, c.la))\n"
          "    return false;\n"
          "  c.virt.lay(c.la, {0}u, {2});\n"
          "  return true;\n"
          "}}\n\n",
          q, a.cfg.terms[impliedTerms[q]].cond, impliedTerms[q], 1u << q);
  }
  bool acts_on(unsigned s, unsigned term) const {
    for (const LrAction& action : a.action_row(s))
      if (action.term == term) return true;
    return false;
  }
  // Whether a state may probe: a flagged mode collects its candidates in the
  // lexer, and an indented grammar reads its token first.
  bool asks(unsigned s) const {
    return !multiMode(a.mode[s]) && !indented() &&
           a.modeLonger[a.mode[s]] == kNoMode;
  }
  // A dispatch with the state's own labels as placeholders: two states that
  // read the same switch share one.
  Piece dispatch_of(unsigned s) {
    Piece d = piece([&] { emit_dispatch(s); });
    const std::string n = std::to_string(s);
    d.text.erase(0, ("D" + n + ":\n").size());
    replace_all(d.text, "goto D" + n + ";", "goto @D;");
    replace_all(d.text, "X" + n + ":", "@X:");
    replace_all(d.text, "goto X" + n + ";", "goto @X;");
    return d;
  }
  static void replace_all(std::string& d, const std::string& from,
                          const std::string& to) {
    for (std::size_t k = d.find(from); k != std::string::npos;
         k = d.find(from, k + to.size()))
      d.replace(k, from.size(), to);
  }
  std::vector<std::uint32_t> dispLead{};
  std::vector<std::uint8_t> dispShared{};
  std::vector<Piece> dispPiece{};
  std::vector<Piece> statePiece{};
  void plan_dispatches() {
    dispLead.assign(a.nstates, 0);
    dispShared.assign(a.nstates, 0);
    dispPiece.assign(a.nstates, {});
    // What it spells and the mode it lexes in tell two apart.
    std::vector<std::unordered_map<std::string_view, std::uint32_t>> first(
        a.nmodes);
    for (unsigned s = 0; s < a.nstates; ++s) {
      dispPiece[s] = dispatch_of(s);
      const auto [it, fresh] =
          first[a.mode[s]].try_emplace(dispPiece[s].text, s);
      dispLead[s] = it->second;
      if (!fresh) dispShared[s] = dispShared[it->second] = 1;
    }
  }

  // Whether other states enter this one's reading, at `L`.
  bool shares(unsigned s) const {
    return dispShared[s] != 0 && dispLead[s] == s;
  }
  // `S` after a shift, then `L` where others enter.
  void emit_entry_labels(unsigned s) {
    if (shifted(s)) fmt("S{}:\n", s);
    if (shares(s)) fmt("L{}:\n", s);
  }

  void emit_state(unsigned s) {
    // A state whose reading another spells enters it.
    if (dispShared[s] != 0 && dispLead[s] != s) {
      const unsigned lead = dispLead[s];
      fmt("G{}: {{\n  goto G{};\n", s, lead);
      if (shifted(s)) fmt("S{}:\n  goto L{};\n", s, lead);
      line("}");
      return;
    }
    fmt("G{}: {{\n", s);
    if (asks(s) && (shifted(s) || shares(s)))
      emit_entry_probe(s);
    else
      emit_plain_lex(s);
    fmt("D{}:\n", s);
    if (dispShared[s]) fmt("DS{}:\n", s);
    std::string d = dispPiece[s].text;
    replace_all(d, "@D",
                std::string(dispShared[s] ? "DS" : "D") + std::to_string(s));
    replace_all(d, "@X", "X" + std::to_string(s));
    raw(d);
  }

  // The one state each nonterminal's column leads to, or -1.
  std::vector<int> loneTo{};
  void plan_lone_gotos() {
    loneTo.assign(a.cfg.nnts, -1);
    for (unsigned nt = 0; nt < a.cfg.nnts; ++nt) {
      const auto column = gotos[nt];
      if (column.empty()) continue;
      const auto to = column[0].to;
      const bool lone = std::ranges::all_of(
          column, [&](const auto e) { return e.guard == 0 && e.to == to; });
      if (lone) loneTo[nt] = static_cast<int>(to);
    }
  }
  int lone_goto(unsigned nt) const { return loneTo[nt]; }

  // The data, helpers and lexers the states read, ahead of them in one unit.
  void emit_helpers(std::string_view ns) {
    const LexicalPredicateEmitter lexical(
        a.cfg, ::std::span(a.relays.data(), a.nrelays),
        "::cursiv::lex_boundary(kTable.cfg)");
    raw(R"(// GENERATED by the native parser emitter -- do not edit.
// Every state keeps its labels, reached or not: the file maps the table.
// Labels: G/S/L entry, C lexed, D/DS dispatch, X refused, H/P hop, AC arm,
// R reduce, T shared landing, Acc accept.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-label"
#else
#pragma GCC diagnostic ignored "-Wunused-label"
#endif
#include <bit>
#include <cstdint>
#include <cstring>
)");
    line("#include <cassert>");
    line("#include <algorithm>");
    if (conds() || a.nrelays != 0) {
      line("#include <string_view>");
      line("#include <utility>");
      line("#include \"cursiv/engines/shared/runtime/PredicateState.hpp\"");
    }
    if (lexical.any()) {
      line("#include <exception>");
      line("#include \"cursiv/engines/shared/runtime/LexicalMachine.hpp\"");
    }
    if (lexical.searches())
      line("#include \"cursiv/engines/shared/runtime/lex_find.hpp\"");
    if (conds())
      line("#include \"cursiv/engines/shared/runtime/LexSuffixSession.hpp\"");
    if (a.multiCand && !(conds() || a.nrelays != 0))
      line("#include <utility>");
    raw(R"(
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrExit.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
)");
    line("#include \"Table.hpp\"");
    line("#include \"cursiv/engines/shared/runtime/Scan.hpp\"");
    line("#include \"cursiv/engines/shared/runtime/LexBoundary.hpp\"");
    if (anyPartialLadder())
      line("#include \"cursiv/engines/shared/runtime/LexMatch.hpp\"");
    if (a.multiCand)
      line("#include \"cursiv/engines/lr/runtime/LrLexCandidates.hpp\"");
    line("#include \"cursiv/engines/lr/runtime/lr_matcher.hpp\"");
    if (a.modeFlagPool.sets.size() != 0)
      line("#include \"cursiv/engines/lr/runtime/lr_lex_exact.hpp\"");
    if (a.nrelays != 0) {
      line("#include \"cursiv/engines/shared/runtime/LexRelayRun.hpp\"");
    }
    if (conds() || a.nrelays != 0)
      line("#include \"cursiv/engines/shared/runtime/GapPositions.hpp\"");
    line("#include \"cursiv/engines/lr/runtime/lr_reduce.hpp\"");
    if (!a.guards.empty())
      line("#include \"cursiv/engines/lr/runtime/lr_guard.hpp\"");
    raw(R"(#include "cursiv/engines/lr/runtime/lr_moves.hpp"
#include "cursiv/engines/lr/table/LrFusedRow.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

)");
    fmt("namespace {} {{\n\n", ns);
    line("::cursiv::LrExit drive_state(::cursiv::LrContext&);");
    line("");
    emit_swar();
    emit_data();
    emit_predicate_data();
    raw(lexical.source());
    {
      std::string done(a.nmodes, '\0');
      for (unsigned st = 0; st < a.nstates; ++st) done[a.mode[st]] = 1;
      emit_walk();
      emit_relay_runs();
      if (conds()) {
        raw(gapEmitter.source());
        raw(predicateEmitter.source(
            "  if (seen) {\n"
            "    ::cursiv::TextOffset halt = at;\n"
            "    const auto reading = ::cursiv::lr_match_halt<kTable>(src, len, "
            "mode, at, halt);\n"
            "    *seen = ::std::max(*seen, halt < len ? halt + 1 : len);\n"
            "    return reading;\n"
            "  }\n"
            "  return walk_all(src, len, mode, at);\n",
            "::cursiv::LrLexTape<kTable>{}"));
      }
      plan_lexers(done);
      if (a.longer_modes()) emit_lengthen();
      for (unsigned m = 0; m < a.nmodes; ++m)
        if (done[m]) emit_lexer(m);
    }
    line("");
  }

  // Groups grown by affinity over the states and reduces that link.
  void grow_groups(const std::vector<unsigned>& weight,
                   const std::vector<unsigned>& nodes, std::uint64_t total,
                   std::vector<unsigned>& part) {
    const unsigned ns = a.nstates, n = ns + a.cfg.nprods;
    std::vector<std::vector<unsigned>> adj(n);
    const auto link = [&](unsigned x, unsigned y) {
      adj[x].push_back(y);
      adj[y].push_back(x);
    };
    for (unsigned s = 0; s < ns; ++s) {
      for (const LrAction& x : a.action_row(s)) {
        if (x.kind == LrActionKind::shift) link(s, x.arg);
        if (x.kind == LrActionKind::reduce) link(s, ns + reduceOf[x.arg]);
        if (x.kind == LrActionKind::guard)
          for (const std::uint16_t to : a.guards[x.arg].to)
            if (to != LrGuard::kNoState) link(s, to);
      }
    }
    for (unsigned q = 0; q < a.cfg.nprods; ++q) {
      if (weight[ns + q] == 0) continue;
      const unsigned lhs = a.cfg.prods[q].lhs;
      for (const auto e : gotos[lhs]) {
        link(ns + q, e.to);
        if (e.guard != 0)
          for (const std::uint16_t to : a.guards[e.guard - 1u].to)
            if (to != LrGuard::kNoState) link(ns + q, to);
      }
    }
    // States that read one dispatch go to one group: the jump into the lead's
    // reading stays inside one function, as the weights above assumed.
    std::vector<std::vector<unsigned>> sharers(ns);
    for (unsigned s = 0; s < ns; ++s)
      if (dispShared[s] != 0) sharers[dispLead[s]].push_back(s);
    // Grown by affinity: the next node is the one most linked to the group.
    std::vector<unsigned> seeds = nodes;
    std::ranges::stable_sort(seeds, [&](unsigned x, unsigned y) {
      return adj[x].size() > adj[y].size();
    });
    std::vector<unsigned> gain(n, 0);
    std::vector<unsigned> front;
    std::size_t nextSeed = 0;
    for (unsigned g = 0; g < ngroups; ++g) {
      const std::uint64_t cap = total * (g + 1) / ngroups;
      std::uint64_t acc = total * g / ngroups;
      const auto place = [&](unsigned v) {
        part[v] = g;
        acc += weight[v];
        for (const unsigned y : adj[v]) {
          if (part[y] != ~0u || weight[y] == 0) continue;
          if (gain[y]++ == 0) front.push_back(y);
        }
      };
      while (acc < cap || g + 1 == ngroups) {
        unsigned pick = ~0u;
        for (const unsigned v : front)
          if (part[v] == ~0u && (pick == ~0u || gain[v] > gain[pick]))
            pick = v;
        if (pick == ~0u) {
          while (nextSeed < seeds.size() && part[seeds[nextSeed]] != ~0u)
            ++nextSeed;
          if (nextSeed == seeds.size()) break;
          pick = seeds[nextSeed];
        }
        if (pick < ns && dispShared[pick] != 0)
          for (const unsigned v : sharers[dispLead[pick]]) place(v);
        else
          place(pick);
      }
      for (const unsigned v : front) gain[v] = 0;
      front.clear();
    }
  }

  // What a group holds: its states, and the reduces that land from it.
  void plan_groups(const std::string& used) {
    const unsigned ns = a.nstates, n = ns + a.cfg.nprods;
    // Each node weighs the lines it writes in a single group; a node that
    // writes none is not one.
    std::vector<unsigned> weight(n, 0);
    const auto newlines = [](std::string_view t) {
      return static_cast<unsigned>(std::ranges::count(t, '\n'));
    };
    const auto lines_of = [&](auto&& f) { return 1u + newlines(capture(f)); };
    for (unsigned s = 0; s < ns; ++s)
      weight[s] = lines_of([&] { emit_hop(s, {}); }) +
                  newlines(statePiece[s].text);
    for (unsigned q = 0; q < a.cfg.nprods; ++q)
      if (used[q] && reduceOf[q] == q)
        weight[ns + q] = lines_of([&] { emit_reduce(q, {}, std::nullopt); }) +
                         newlines(tailPiece[q].text);
    std::uint64_t total = 0;
    std::vector<unsigned> nodes;
    for (unsigned v = 0; v < n; ++v)
      if (weight[v] != 0) {
        nodes.push_back(v);
        total += weight[v];
      }
    ngroups = static_cast<unsigned>((total + groupLines - 1) / groupLines);
    ngroups = std::max(1u, std::min<unsigned>(ngroups, ns));
    std::vector<unsigned> part(n, ~0u);
    if (ngroups == 1)
      for (const unsigned v : nodes) part[v] = 0;
    else
      grow_groups(weight, nodes, total, part);
    // Every group but the last starts below its share, so only the tail can
    // be left empty once the nodes run out.
    ngroups = 0;
    for (const unsigned v : nodes) ngroups = std::max(ngroups, part[v] + 1);
    groupOf = std::move(part);
    localOf.assign(ns, 0);
    entries.assign(ngroups, {});
    entryAt.assign(ngroups, {});
    for (unsigned s = 0; s < ns; ++s) {
      const unsigned g = groupOf[s];
      localOf[s] = static_cast<unsigned>(entries[g].size());
      auto label = std::format("G{}", s);
      entryAt[g].emplace(label, localOf[s]);
      entries[g].push_back(std::move(label));
    }
  }

  // One group's function, without its entry table; whether a reduce in it
  // fills a record.
  bool emit_group_body(unsigned g, const std::string& used) {
    ProbeSet probes;
    std::vector<unsigned> mine;
    for (unsigned st = 0; st < a.nstates; ++st) {
      if (groupOf[st] != g) continue;
      mine.push_back(st);
      enter(g, statePiece[st]);
      probes.insert(probesOf[st].begin(), probesOf[st].end());
    }
    for (const unsigned st : mine) emit_hop(st, probes);
    for (const unsigned st : mine) raw(resolved(statePiece[st].text, g));
    const Piece hops = piece([&] { emit_probe_hops(probes); });
    enter(g, hops);
    raw(resolved(hops.text, g));
    line("Acc:");
    line("  flush(c);");
    fmt("  {}\n", leave("accept"));
    std::vector<unsigned> leads;
    for (unsigned prod = 0; prod < a.cfg.nprods; ++prod)
      if (used[prod] && reduceOf[prod] == prod &&
          groupOf[a.nstates + prod] == g)
        leads.push_back(prod);
    std::vector<std::string> tails(leads.size());
    const auto settle = [&](std::size_t i) {
      enter(g, tailPiece[leads[i]]);
      tails[i] = resolved(tailPiece[leads[i]].text, g);
    };
    const auto key = [&](std::size_t i) {
      return TailKey{tails[i],
                     a.reductions[leads[i]].row(a.cfg.prods[leads[i]].len)};
    };
    const auto lone = [&](std::size_t i) {
      return lone_goto(a.cfg.prods[leads[i]].lhs) >= 0;
    };
    std::map<TailKey, unsigned> uses;
    for (std::size_t i = 0; i < leads.size(); ++i)
      if (!lone(i)) {
        settle(i);
        ++uses[key(i)];
      }
    std::map<TailKey, unsigned> sharedTails;
    for (const auto& [k, n] : uses)
      if (n > 1 &&
          std::ranges::count(k.first, '\n') >= std::ptrdiff_t{kLrTailLines})
        sharedTails.emplace(k, static_cast<unsigned>(sharedTails.size()));
    for (std::size_t i = 0; i < leads.size(); ++i)
      if (lone(i)) settle(i);
    for (std::size_t i = 0; i < leads.size(); ++i) {
      const auto it = sharedTails.find(key(i));
      emit_reduce(
          leads[i], tails[i],
          it != sharedTails.end() ? std::optional{it->second} : std::nullopt);
    }
    for (const auto& [k, id] : sharedTails) {
      fmt("T{}:\n", id);
      line("    {");
      raw(k.first);
      line("    }");
    }
    return std::ranges::any_of(leads, [&](unsigned prod) {
      return a.reductions[prod].row(a.cfg.prods[prod].len) != LrReduceRow::none;
    });
  }

  // Every state of the automaton, in groups.
  void emit_states_cpp(std::string_view ns) {
    emit_drive_cond();
    emit_imply();
    plan_hops();
    std::string used(a.cfg.nprods, '\0');
    for (unsigned st = 0; st < a.nstates; ++st)
      for (const LrAction& x : a.action_row(st)) {
        if (x.kind == LrActionKind::reduce) used[x.arg] = 1;
      }
    plan_reduces(used);
    plan_lone_gotos();
    plan_dispatches();
    statePiece.assign(a.nstates, {});
    probesOf.assign(a.nstates, {});
    for (unsigned s = 0; s < a.nstates; ++s)
      statePiece[s] = piece([&] { emit_state(s); });
    render_tails(used);
    plan_groups(used);
    for (unsigned s = 0; s < a.nstates; ++s) enter(groupOf[s], dispPiece[s]);
    std::vector<std::string> bodies(ngroups);
    std::vector<bool> holdsRec(ngroups, false);
    for (unsigned g = 0; g < ngroups; ++g)
      bodies[g] = capture([&] { holdsRec[g] = emit_group_body(g, used); });
    // Saving nothing makes a jump between groups a bare jump; a lone group
    // has no such jump, and its code grows under it.
    const auto head = [&](unsigned g) {
      fmt("CURSIV_NOINLINE {}static ::cursiv::LrExit "
          "states_{}(::cursiv::LrContext& c, ::std::uint32_t e)",
          ngroups > 1 ? "CURSIV_PRESERVE_NONE " : "", g);
    };
    // Under ASan every local is addressable, and GCC suspects each one at a
    // tail call although no helper keeps the address it is handed.
    if (ngroups > 1) {
      raw(R"(#ifndef CURSIV_MUSTTAIL
#error "cursiv: states in groups need a tail call the compiler guarantees"
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-musttail-local-addr"
#endif
)");
      for (unsigned g = 0; g < ngroups; ++g) {
        head(g);
        line(";");
      }
      line("");
    }
    for (unsigned g = 0; g < ngroups; ++g) {
      head(g);
      raw(R"( {
  const char* const src = c.src;
  const ::cursiv::TextOffset len = c.len;
  ::cursiv::TextOffset hb = 0, he = 0;
  (void)src;
  (void)len;
  (void)hb;
  (void)he;
)");
      if (holdsRec[g])
        raw("  ::cursiv::ElementId rec = ::cursiv::kNoElement;\n"
            "  (void)rec;\n");
      raw("  static const void* const kIn[] = {");
      for (std::size_t k = 0; k < entries[g].size(); ++k) {
        if (k) out += ',';
        if (k % 12 == 0) out += '\n';
        raw("&&");
        raw(entries[g][k]);
      }
      line("};");
      line("  goto* kIn[e];");
      raw(bodies[g]);
      line("}");
      line("");
    }
    // A lone group's entries are the states in order.
    if (ngroups > 1)
      array("inline constexpr ::std::uint32_t kEnter[]", a.nstates,
            [&](std::size_t s) { num((groupOf[s] << 16) | localOf[s]); }, "",
            true);
    line("::cursiv::LrExit drive_state(::cursiv::LrContext& c) {");
    if (ngroups == 1) {
      line("  return states_0(c, c.stack.top().state);");
    } else {
      line("  const ::std::uint32_t e = kEnter[c.stack.top().state];");
      line("  switch (e >> 16) {");
      for (unsigned g = 0; g + 1 < ngroups; ++g)
        fmt("  case {0}: return states_{0}(c, e & 0xFFFFu);\n", g);
      fmt("  default: return states_{}(c, e & 0xFFFFu);\n", ngroups - 1);
      line("  }");
    }
    raw(R"(}

::cursiv::Scan drive_walk(const char* src, ::cursiv::TextOffset len,
                          ::std::uint16_t mode, ::cursiv::TextOffset from) {
  return ::cursiv::lex_longest(
      ::cursiv::LrLexTape<kTable>{}, mode, walk_all(src, len, mode, from),
      [&](::std::uint16_t more) { return walk_all(src, len, more, from); });
}
)");
    raw(R"(::cursiv::ElementId drive_folds(::cursiv::ElementId held,
                              ::cursiv::ElementId over) {
  return folds(held, over);
}
)");
    if (a.multiCand)
      raw(R"(
// The sweep, for a reading that is not the parse: it fills the caller's own
// room, since the running position's candidates are still in hand.
::std::uint32_t scan_text(const char* src, ::cursiv::TextOffset len,
                        ::std::uint16_t mode, ::cursiv::TextOffset from,
                        ::cursiv::Scan* out, ::std::uint32_t cap) {
  ::std::uint32_t n = 0;
  cands_at(src, len, mode, from, out, n, cap);
  rank_cands(out, n);
  return n;
}
)");
    line("");
    fmt("}}  // namespace {}\n", ns);
    settle_swar();
  }

  void emit_all(std::string_view ns) {
    plan_laid();
    emit_helpers(ns);
    emit_states_cpp(ns);
  }
};

struct LrParserDecl : EmitOutput {
  explicit LrParserDecl(const LrBuildTable& table) : a(table) {}

  const LrBuildTable& a;
  GapReach<LrBuildCfg> gapReach{a.cfg};
  bool conds() const { return a.cfg.ngapConds != 0; }

  void emit(std::string_view ns) {
    raw(R"(// GENERATED by the native parser emitter -- do not edit.
#pragma once

#include "cursiv/engines/lr/runtime/LrDrive.hpp"

)");
    fmt("namespace {} {{\n", ns);
    raw(R"(::cursiv::LrExit drive_state(::cursiv::LrContext&);
::cursiv::Scan drive_walk(const char*, ::cursiv::TextOffset, ::std::uint16_t,
                          ::cursiv::TextOffset);
)");
    line(
        "::cursiv::ElementId drive_folds(::cursiv::ElementId,"
        " ::cursiv::ElementId);");
    if (conds()) {
      line("bool drive_cond(const ::cursiv::LrContext&, unsigned, "
           "::cursiv::TextOffset, const ::cursiv::Token&);");
      line("bool caret_cond(::std::string_view, unsigned, "
           "::cursiv::TextOffset, const ::cursiv::Token&, bool, "
           "::cursiv::LrGapMemo*);");
      if (gapReach.skips_any())
        line(
            "::cursiv::TextOffset caret_seen(::std::string_view, unsigned, "
            "::cursiv::TextOffset, const ::cursiv::Token&);");
    }
    if (a.multiCand)
      raw(R"(::std::uint32_t scan_text(const char*, ::cursiv::TextOffset, ::std::uint16_t,
                        ::cursiv::TextOffset, ::cursiv::Scan*, ::std::uint32_t);
)");
    if (conds()) {
      raw("inline constexpr ::cursiv::CondReach kCondReach[] = {");
      for (unsigned k = 0; k < a.cfg.ngapConds; ++k) {
        const CondReach r = gapReach.of(k);
        fmt("{}{{{}u, {}u, {}, {}u}}", k != 0 ? ", " : "", r.look, r.tail,
            r.phrase ? "true" : "false", r.tokens);
      }
      line("};");
    }
    raw(R"(inline constexpr ::cursiv::LrDrive kDrive{
    .enter = &drive_state,
    .walk = &drive_walk,
)");
    line(a.multiCand ? "    .scanText = &scan_text,"
                    : "    .scanText = nullptr,");
    line("    .folds = &drive_folds,");
    line(conds() ? "    .cond = &drive_cond," : "    .cond = nullptr,");
    if (conds()) {
      line("    .condText = &caret_cond,");
      line("    .condReach = kCondReach,");
      if (gapReach.skips_any()) line("    .condSeen = &caret_seen,");
    }
    line("};");
    raw(R"(inline constexpr const ::cursiv::LrDrive* kDrivePtr = &kDrive;
)");
    fmt("}}  // namespace {}\n", ns);
  }
};

}  // namespace

// Every unit of one grammar's generated parser, named.
std::vector<std::pair<std::string, std::string>> lr_emit_parser_files(
    const LrBuildTable& a, std::string_view ns, const LrProgramLayout& programs,
    unsigned* probeCuts, unsigned groupLines) {
  if (a.reductions.size() != a.cfg.nprods || a.readsToken.size() != a.nstates ||
      a.skipSet.size() != std::size_t{a.nmodes} * 4)
    throw std::logic_error("cursiv: derive LR tables before emitting the parser");
  LrParserEmitter e{a, programs};
  e.groupLines = groupLines;
  e.out.reserve(1u << 20);
  e.emit_all(ns);
  if (probeCuts != nullptr) *probeCuts = e.probeCuts;
  std::vector<std::pair<std::string, std::string>> files;
  files.emplace_back("ParserStates.cpp", std::move(e.out));
  return files;
}

std::string lr_emit_parser_decl(const LrBuildTable& a, std::string_view ns) {
  LrParserDecl d{a};
  d.emit(ns);
  return d.out;
}

}  // namespace cursiv
