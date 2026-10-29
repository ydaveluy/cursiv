// The frozen table of a grammar, written as C++ data: every array at its
// exact extent, only what a reader takes.
#include "cursiv/engines/lr/gen/lr_emit_table.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/engines/lr/gen/LrFinalTable.hpp"
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"
#include "cursiv/engines/lr/gen/LrProgramMatrix.hpp"
#include "cursiv/engines/lr/table/LrDerivedSizes.hpp"
#include "cursiv/engines/lr/table/LrLowSizes.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LexicalPredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

namespace {

LrSizes lr_sizes(const LrBuildTable& table, const LrProgramLayout& layout) {
  LrSizes sizes = table.sizes();
  sizes.orderWords = static_cast<unsigned>(layout.exact.size());
  return sizes;
}

std::vector<std::uint8_t> lr_guarded_columns(const LrBuildTable& table) {
  std::vector<std::uint8_t> guarded(table.cfg.nnts, 0);
  for (const auto& e : table.gotoRows.edges)
    if (e.guard != 0) guarded[e.nt] = 1;
  return guarded;
}

struct LrTableEmitter : EmitOutput {
  explicit LrTableEmitter(const LrFinalTable& done)
      : a(done.table()),
        guarded(lr_guarded_columns(a)),
        S(lr_sizes(a, done.programs())),
        programs(done.programs()),
        names(a.cfg, std::span(a.relays.data(), a.nrelays)) {}

  const LrBuildTable& a;
  const std::vector<std::uint8_t> guarded;
  const LrSizes S;
  bool aggregate = false;
  const LrProgramLayout& programs;

  const LexNamePool names;

  LrLowSizes low_sizes() const {
    const auto& c = a.cfg;
    return LrLowSizes{static_cast<unsigned>(names.bytes.size()), c.nsets,
                      c.nterms, c.nprods,
                      static_cast<unsigned>(c.wordRanges.size()),
                      static_cast<unsigned>(c.scalarRanges.size())};
  }

  void u32(unsigned long long v) { fmt("{}u", v); }
  void u64(unsigned long long v) { fmt("{}ull", v); }

  void cell(char c) { fmt("char({})", static_cast<unsigned char>(c)); }
  void cell(std::uint8_t v) { num(v); }
  void cell(std::uint16_t v) { num(v); }
  void cell(std::uint32_t v) { u32(v); }
  void cell(std::uint64_t v) { u64(v); }
  void cell(const ByteSet& s) {
    raw("{{");
    for (unsigned k = 0; k < 4; ++k) {
      if (k) raw(",");
      u64(s.w[k]);
    }
    raw("}}");
  }
  void cell(const LrLexElse& e) {
    raw("{.bytes=");
    cell(e.bytes);
    fmt(",.term={}", e.term);
    if (e.scalar != 0) fmt(",.scalar={}", e.scalar);
    raw("}");
  }
  // A terminal set at the width the frozen table holds.
  void cell(const DynSet& s) {
    const unsigned words = (S.termBits() + 63u) / 64u;
    raw("{{");
    for (unsigned k = 0; k < words; ++k) {
      if (k) raw(",");
      u64(s.word(k));
    }
    raw("}}");
  }
  // A cell spells only the fields off their default: the member initializers
  // write the rest.
  struct Fields {
    std::string text;
    template <class V>
    void operator()(std::string_view name, V v, V def,
                    std::string_view pre = "", std::string_view post = "") {
      if (v != def)
        std::format_to(std::back_inserter(text), ",.{}={}{}{}", name, pre, v,
                       post);
    }
  };
  void cell(const Fields& f) {
    fmt("{{{}}}", std::string_view(f.text).substr(f.text.empty() ? 0 : 1));
  }
  void cell(const TermInfo& t) {
    constexpr TermInfo d{};
    Fields f;
    f("nameOff", names(t.nameOff), d.nameOff);
    f("nameLen", t.nameLen, d.nameLen);
    f("element", t.element, d.element);
    f("trivia", t.trivia, d.trivia);
    f("recorded", t.recorded, d.recorded);
    f("spelled", t.spelled, d.spelled);
    f("textOff", names(t.textOff), d.textOff);
    f("textLen", t.textLen, d.textLen);
    f("delim", t.delim, d.delim);
    f("joins", t.joins, d.joins);
    f("laid", t.laid, d.laid);
    f("cond", t.cond, d.cond);
    f("refuses", t.refuses, d.refuses);
    cell(f);
  }
  void cell(const LrProduction& p) {
    constexpr LrProduction d{};
    Fields f;
    f("lhs", p.lhs, d.lhs);
    f("node", p.node, d.node);
    f("len", p.len, d.len);
    f("gap", p.gap, d.gap);
    f("guarded", p.guarded, d.guarded);
    cell(f);
  }
  void cell(const LrReducePlan& p) {
    constexpr LrReducePlan d{};
    Fields f;
    f("names", p.names, d.names, "", "u");
    f("count", p.count, d.count);
    f("form", static_cast<std::uint8_t>(p.form),
      static_cast<std::uint8_t>(d.form), "::cursiv::LrReduceForm(", ")");
    f("flushes", p.flushes, d.flushes);
    f("turnRow", p.turnRow, d.turnRow);
    cell(f);
  }
  void cell(const LrReduceName& n) {
    constexpr LrReduceName d{};
    Fields f;
    f("slot", n.slot, d.slot);
    f("row", n.row, d.row);
    f("fold", n.fold, d.fold);
    cell(f);
  }
  void cell(const LexRelayGen& r) { raw(lex_relay_source(r, names)); }
  void cell(const unicode::Range& r) {
    fmt("{{{}u,{}u}}", static_cast<std::uint32_t>(r.lo),
        static_cast<std::uint32_t>(r.hi));
  }
  void cell(const LrExpect& e) {
    fmt("{{.feature={},.site={},.landing={}}}", e.feature, e.site, e.landing);
  }

  // The first `n` cells of `src`, sixteen a line.
  template <class Src>
  void cells(const Src& src, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      if (i) raw(",");
      if (i % 16 == 0) raw("\n    ");
      cell(src[i]);
    }
  }
  // `member = {{cells}};` -- `n` cells of the source, the rest defaulted.
  template <class Src>
  void array(std::string_view member, const Src& src, std::size_t n,
             std::size_t have) {
    std::size_t count = std::min(n, have);
    using Cell = std::remove_cvref_t<decltype(src[0])>;
    if constexpr (std::is_integral_v<Cell> && !std::is_same_v<Cell, char>) {
      while (count != 0 && src[count - 1] == 0) --count;
      if (count == 0) return;
    }
    raw(aggregate ? "  ." : "  t.");
    raw(member);
    if (count == 0) {
      line(aggregate ? " = {}," : " = {};");
      return;
    }
    raw(" = {{");
    cells(src, count);
    line(aggregate ? "}}," : "}};");
  }
  template <class Src>
  void array(std::string_view member, const Src& src,
             std::size_t n = ~std::size_t{0}) {
    array(member, src, n, src.size());
  }

  void emit_sizes() {
    fmt("inline constexpr ::cursiv::LrSizes "
        "kSizes{{.terms={},.prods={},.states={},.modes={},.cls={},.dfa={},"
        ".dfaAccSets={},.dfaResSets={},.modeWinSets={}",
        S.terms, S.prods, S.states, S.modes, S.cls, S.dfa, S.dfaAccSets,
        S.dfaResSets, S.modeWinSets);
    const auto field = [&](std::string_view name, unsigned n) {
      if (n != 0) fmt(",.{}={}", name, n);
    };
    field("modeLongers", S.modeLongers);
    field("modeBacktracks", S.modeBacktracks);
    field("modeFlagSets", S.modeFlagSets);
    fmt(",.accBounds={},.relays={},.followSets={},.closeSets={}", S.accBounds,
        S.relays, S.followSets, S.closeSets);
    field("dfaRelayResSets", S.dfaRelayResSets);
    field("accStands", S.accStands);
    field("accTiers", S.accTiers);
    field("errModes", S.errModes);
    if (S.indented) raw(",.indented=true");
    field("insertLists", S.insertLists);
    field("insertWords", S.insertWords);
    field("electWords", S.electWords);
    field("electWatches", S.electWatches);
    field("electChains", S.electChains);
    field("electChainWords", S.electChainWords);
    field("electTwins", S.electTwins);
    field("electRhsSyms", S.electRhsSyms);
    field("orderWords", S.orderWords);
    if (!S.gotoClasses) raw(",.gotoClasses=false");
    line("};");
    const LrLowSizes L = low_sizes();
    fmt("inline constexpr ::cursiv::LrLowSizes kLow{{{},{},{},{},{}{}}};\n",
        L.nameChars, L.sets, L.terms, L.prods, L.wordRanges,
        L.scalarRanges != 0 ? std::format(",{}", L.scalarRanges)
                            : std::string());
  }

  void emit_cold() {
    aggregate = true;
    line("inline constexpr ::cursiv::LrColdTables<kSizes> kCold{");
    emit_pool_member("follow", a.followPool);
    array("accBoundBase", a.accBoundBase, S.dfa + 1u);
    array("accBound", a.accBound, S.accBounds);
    if (S.accStands != 0) array("accStand", a.accStand, S.accStands);
    if (S.accTiers != 0) array("accTier", a.accTier, S.accTiers);
    array("soleReduce", a.soleReduce, S.states);
    array("readsToken", a.readsToken, S.states);
    if (S.gotoClasses) array("gotoClass", a.gotoClass, S.states);
    array("closePool", a.closePool.sets);
    array("closeOf", a.closePool.of);
    fmt("  .closeMax = {}u,\n", a.closeMax);
    if (S.insertWords != 0) {
      array("insertOf", a.insertOf, S.states);
      array("insertAt", a.insertAt);
      array("insertWord", a.insertWord);
    }
    if (S.electWords != 0) {
      array("electAt", a.electAt);
      array("electWord", a.electWord);
      array("electPos", a.electPos);
      array("electSym", a.stateSym, S.states);
      array("electBelow", a.stateBelow, S.states);
    }
    if (S.orderWords != 0) array("orderExact", programs.exact);
    if (S.electWatches != 0) {
      array("electWatchAt", a.electWatchAt);
      array("electWatch", a.electWatch);
    }
    if (S.electChains != 0) {
      array("electChainKey", a.electChainKey);
      array("electChainAt", a.electChainAt);
      array("electChainWord", a.electChainWord);
      array("electChainPos", a.electChainPos);
    }
    if (S.electTwins != 0) {
      array("electTwinAt", a.electTwinAt);
      array("electTwin", a.electTwin);
      array("electRhsAt", a.electRhsAt);
      array("electRhs", a.electRhs);
    }
    line("};");
    aggregate = false;
  }

  void emit_facts() {
    line("  static_cast<::cursiv::LrTableFacts&>(t) = ::cursiv::LrTableFacts{");
    fmt("    .nstates={},.nconflicts={},.npeeks={},"
        "\n    .cls={{",
        a.nstates, a.nconflicts, a.npeeks);
    for (unsigned b = 0; b < 256; ++b) fmt("{}{}", b ? "," : "", a.cls[b]);
    fmt("}},\n    .ncls={},.multiCand={},"
        "\n    .errMode={},.maxCands={},"
        ".maxArms={}",
        a.ncls, a.multiCand, a.errMode, a.maxCands, a.maxArms);
    if (a.stamp != StampLayout{})
      fmt(",\n    .stamp={{.rowBits={}}}", a.stamp.rowBits);
    line("};");
  }

  void emit_cfg() {
    const auto& c = a.cfg;
    line("  static_cast<::cursiv::LrLowFacts&>(t.cfg) = ::cursiv::LrLowFacts{");
    fmt("    .nterms={},.nnts={},.nprods={},.entryRow={},.wordBytes=", c.nterms,
        c.nnts, c.nprods, c.entryRow);
    cell(c.wordBytes);
    line("};");
    const LrLowSizes L = low_sizes();
    array("cfg.names", names.bytes, L.nameChars);
    array("cfg.sets", c.sets, L.sets);
    array("cfg.setDecode", c.setDecode, L.sets);
    array("cfg.terms", c.terms, L.terms);
    std::vector<LrProduction> prods(c.nprods);
    for (unsigned p = 0; p < c.nprods; ++p)
      prods[p] = {c.prods[p].lhs, c.prods[p].node, c.prods[p].len,
                  c.prods[p].gap, guarded[c.prods[p].lhs]};
    array("cfg.prods", prods, L.prods);
    array("cfg.wordRanges", c.wordRanges, L.wordRanges);
    if (L.scalarRanges != 0) {
      std::vector<std::uint32_t> scalars(c.nsets);
      for (std::uint16_t i = 0; i < c.nsets; ++i)
        scalars[i] = lex_set_scalars(c, i);
      array("cfg.setScalars", scalars);
      array("cfg.scalarRanges", c.scalarRanges, L.scalarRanges);
    }
  }

  void emit_automaton() {
    line("inline constexpr Table kTable = [] {");
    line("  Table t{};");
    emit_facts();
    emit_cfg();
    array("mode", a.mode, S.states);
    array("modeStart", a.modeStart, S.modes);
    array("modeMulti", a.modeMulti, S.modes);
    if (S.modeBacktracks != 0)
      array("modeBacktrack", a.modeBacktrack, S.modeBacktracks);
    if (S.modeLongers != 0) array("modeLonger", a.modeLonger, S.modes);
    emit_pool("modeWin.pool", "modeWin.of", a.modeWinPool);
    if (S.modeFlagSets != 0)
      emit_pool("modeFlag.pool", "modeFlag.of", a.modeFlagPool);
    array("modeTight", a.modeTight, S.modes);
    if (S.errModes != 0) {
      array("errModeOf", a.errModeOf, S.errModes);
      array("modeEnd", a.modeEnd, S.errModes);
    }
    if (!S.indented)
      array("modeSub", a.modeSub, (std::size_t{S.modes} * S.modes + 63) / 64);
    array("dfaNext", a.dfaNext, std::size_t{S.dfa} * S.cls);
    array("dfaAccept", a.dfaAccept, S.dfa);
    emit_pool("dfaAcc.pool", "dfaAcc.of", a.dfaAccPool);
    emit_pool("dfaRes.pool", "dfaRes.of", a.dfaResPool);
    if (S.dfaRelayResSets != 0)
      emit_pool("dfaRelayRes.pool", "dfaRelayRes.of", a.dfaRelayResPool);
    array("dfaBound", a.dfaBound, S.dfa);
    if (S.relays != 0) {
      array("dfaRelay", a.dfaRelay, S.dfa);
      array("relays", a.relays, S.relays);
    }
    line("  return t;");
    line("}();");
  }

  void emit_derived() {
    const auto D = a.derivedSizes();
    fmt("inline constexpr ::cursiv::LrDerivedSizes "
        "kDerivedSizes{{.lexElseRows={},"
        ".typoSucc={},.reduceNames={},"
        ".expectItems={},.expectPool={},.rowLhsRows={},.rowLhs={},"
        ".typoReachSets={},.afterSets={},.modeWantSets={}}};\n",
        D.lexElseRows, D.typoSucc, D.reduceNames, D.expectItems, D.expectPool,
        D.rowLhsRows, D.rowLhs, D.typoReachSets, D.afterSets, D.modeWantSets);
    line(
        "inline constexpr ::cursiv::LrDerivedTables<kSizes, kDerivedSizes> "
        "kDerived{");
    aggregate = true;
    array("skipSet", a.skipSet);
    array("lexElseBase", a.lexElseBase);
    array("lexElseRows", a.lexElseRows);
    array("typoSuccBase", a.typoSuccBase);
    array("typoSucc", a.typoSucc);
    array("reductions", a.reductions);
    array("reduceNames", a.reduceNames);
    line("  .expect = {");
    array("base", a.expectBase);
    array("items", a.expectItems);
    array("pool", a.expectPool);
    array("shiftTerm", a.shiftTerm);
    line("  },");
    array("rowLhsBase", a.rowLhsBase);
    array("rowLhs", a.rowLhs);
    emit_pool_member("typoReach", a.typoReach);
    array("lexBodies", a.lexBodies);
    array("lexOwed", a.lexOwed);
    array("lexOpeners", a.lexOpeners);
    emit_pool_member("after", a.after);
    emit_pool_member("modeWant", a.modeWant);
    line("};");
    aggregate = false;
  }

  // A set per index, as the distinct sets and a rank each.
  void emit_pool(std::string_view pool, std::string_view of,
                 const SetPool& set) {
    array(pool, set.sets);
    array(of, set.of);
  }
  // The same as member `name` of an aggregate.
  void emit_pool_member(std::string_view name, const SetPool& set) {
    fmt("  .{} = {{\n", name);
    emit_pool("pool", "of", set);
    line("  },");
  }

  template <class Values>
  void program_array(std::string_view type, std::string_view name,
                     const Values& values) {
    fmt("inline constexpr {} {}[] = {{", type, name);
    cells(values, values.size());
    if (values.empty()) raw("{}");
    line("};");
  }

  void program_matrix(const LrProgramMatrix& matrix, std::string_view name,
                      std::string_view value, unsigned columns,
                      unsigned absent) {
    const auto row = std::format("::std::uint{}_t", matrix.row_bits());
    const auto index = std::format("::std::uint{}_t", matrix.index_bits());
    // A layer the matrix does not lay is an empty span.
    const auto layer = [&](std::string_view type, std::string_view part,
                           const std::vector<std::uint32_t>& cells) {
      if (cells.empty()) return std::string("{}");
      std::string array = std::format("{}{}", name, part);
      program_array(type, array, cells);
      return array;
    };
    const std::string rows = layer(row, "Rows", matrix.rows);
    const std::string blocks = layer(index, "Blocks", matrix.blocks);
    program_array(value, std::format("{}Values", name), matrix.values);
    fmt("inline constexpr ::cursiv::LrProgramRows<{}, {}, {}> {}{{\n"
        "    {}, {}, {}Values, {}u, {}u}};\n",
        row, index, value, name, rows, blocks, name, columns, absent);
  }

  void emit_programs() {
    const auto& p = programs;
    const auto value = std::format("::std::uint{}_t", p.actions.value_bits());
    program_matrix(p.actions, "kActionMatrix", value, S.terms, 0);
    program_matrix(p.gotos, "kGotoPrograms", "::std::uint16_t", a.cfg.nnts,
                   0xffffu);
    const std::uint32_t most =
        p.ops.empty() ? 0 : *std::ranges::max_element(p.ops);
    const auto op = std::format("::std::uint{}_t", LrProgramMatrix::bits(most));
    program_array(op, "kProgramOps", p.ops);
    fmt("inline constexpr ::cursiv::LrActionPrograms<{}, {}, {}, {}> "
        "kPrograms{{\n"
        "    kActionMatrix, kProgramOps, {}u}};\n",
        std::format("::std::uint{}_t", p.actions.row_bits()),
        std::format("::std::uint{}_t", p.actions.index_bits()), value, op,
        p.countBits);
  }

  // Guard `k` lays at `kGuardAt[k]` its condition count, the conditions and a
  // landing per outcome; `kHeld` holds per condition a bit per state, set
  // where its tag says the condition held or says nothing of it.
  void emit_guards() {
    if (a.guards.empty()) return;
    std::vector<std::uint32_t> at;
    std::vector<std::uint16_t> ops;
    for (const LrGuard& g : a.guards) {
      at.push_back(static_cast<std::uint32_t>(ops.size()));
      ops.push_back(static_cast<std::uint16_t>(g.conds.size()));
      ops.insert(ops.end(), g.conds.begin(), g.conds.end());
      ops.insert(ops.end(), g.to.begin(), g.to.end());
    }
    program_array("::std::uint32_t", "kGuardAt", at);
    program_array("::std::uint16_t", "kGuardOps", ops);
    const std::size_t words = (std::size_t{a.nstates} + 63u) / 64u;
    std::vector<std::uint64_t> held(words * a.cfg.ngapConds, 0);
    for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
      for (unsigned s = 0; s < a.nstates; ++s)
        if (!a.tagKnown[s].test(k) || a.tagTruth[s].test(k))
          held[k * words + s / 64u] |= std::uint64_t{1} << (s % 64u);
    program_array("::std::uint64_t", "kHeld", held);
  }

  static bool any_words(const std::vector<HeadMatcher>& heads) {
    return std::ranges::any_of(
        heads, [](const HeadMatcher& h) { return !h.words.empty(); });
  }
  bool refusing() const { return any_words(a.termHeads); }
  bool trailing() const { return any_words(a.termTails); }

  void emit(std::string_view ns) {
    raw(R"(// GENERATED by the native table emitter -- do not edit.
// The frozen table of one grammar, as data: the engine's whole reading of it.
#pragma once
#include <cstdint>

#include "cursiv/engines/lr/table/LrColdTables.hpp"
#include "cursiv/engines/lr/table/LrDerivedTables.hpp"
#include "cursiv/engines/lr/table/LrFrozenTable.hpp"

)");
    line("#include \"cursiv/engines/lr/table/LrActionPrograms.hpp\"");
    if (refusing()) {
      line("#include <algorithm>");
      line("#include \"cursiv/core/utils/Utf8.hpp\"");
      line("#include \"cursiv/core/utils/macros.hpp\"");
      line("#include \"cursiv/engines/shared/runtime/lex_scalar_in.hpp\"");
    }
    if (!a.guards.empty()) line("#include <span>");
    fmt("namespace {} {{\n\n", ns);
    raw(LexicalPredicateEmitter(a.cfg, ::std::span(a.relays.data(), a.nrelays),
                                "")
            .declarations());
    emit_sizes();
    emit_cold();
    emit_derived();
    emit_programs();
    emit_guards();
    raw("struct Table : ::cursiv::LrFrozenTable<kSizes, kLow, kCold, "
        "kDerived> {\n"
        "  static constexpr auto& programs = kPrograms;\n"
        "  static constexpr auto& gotoPrograms = kGotoPrograms;\n");
    if (!a.guards.empty())
      raw("  static constexpr ::std::span<const ::std::uint32_t> guardAt{"
          "kGuardAt};\n"
          "  static constexpr ::std::span<const ::std::uint16_t> guardOps{"
          "kGuardOps};\n"
          "  static constexpr ::std::span<const ::std::uint64_t> held{kHeld};\n");
    if (refusing())
      raw("  static bool head_refuses(::cursiv::SymbolId t, const char* p,\n"
          "                           ::cursiv::TextOffset rest);\n"
          "  static ::cursiv::TextOffset head_reach(unsigned m, const char* p,\n"
          "                                         ::cursiv::TextOffset rest);\n");
    if (const auto words = lex_word_bounds(a.cfg);
        !words.empty() && (refusing() || a.cfg.sections())) {
      fmt("  static constexpr ::std::uint16_t kWordBounds[] = {{{}u", words[0]);
      for (std::size_t i = 1; i < words.size(); ++i) fmt(", {}u", words[i]);
      raw("};\n"
          "  static constexpr ::std::span<const ::std::uint16_t> wordBounds{"
          "kWordBounds};\n");
    }
    if (trailing())
      raw("  static bool tail_refuses(::cursiv::SymbolId t, const char* p,\n"
          "                           ::cursiv::TextOffset rest);\n");
    if (a.nrelays != 0)
      raw("  static ::cursiv::TextOffset relay_run(::std::uint16_t, const "
          "char*,\n"
          "      ::cursiv::TextOffset, ::cursiv::TextOffset);\n");
    line("};");
    emit_automaton();
    if (refusing())
      raw(lex_head_source(
          a.termHeads, a.modeHeads,
          "inline bool Table::head_refuses(::cursiv::SymbolId t, "
          "const char* p,\n    ::cursiv::TextOffset rest)",
          "inline ::cursiv::TextOffset Table::head_reach(unsigned m, "
          "const char* p,\n    ::cursiv::TextOffset rest)",
          {"kTable.cfg.wordRanges.data(), kTable.cfg.wordRanges.size()",
           "kTable.cfg.scalarRanges.data()"}));
    if (trailing())
      raw(lex_tail_source(
          a.termTails,
          "inline bool Table::tail_refuses(::cursiv::SymbolId t, "
          "const char* p,\n    ::cursiv::TextOffset rest)",
          {"kTable.cfg.wordRanges.data(), kTable.cfg.wordRanges.size()",
           "kTable.cfg.scalarRanges.data()"}));
    fmt("\n}}  // namespace {}\n", ns);
  }
};

}  // namespace

std::string lr_emit_table(const LrFinalTable& done, std::string_view ns) {
  LrTableEmitter e{done};
  e.out.reserve(1u << 20);
  e.emit(ns);
  return e.out;
}

}  // namespace cursiv
