// The frozen table of a grammar, written as C++ data: every array at its
// exact extent, only what a reader takes.
#include "cursiv/lr/gen/lr_emit_table.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/lr/gen/LrFinalTable.hpp"
#include "cursiv/lr/gen/LrProgramLayout.hpp"
#include "cursiv/lr/gen/LrProgramMatrix.hpp"
#include "cursiv/lr/table/LrProduction.hpp"
#include "cursiv/lr/table/LrSizes.hpp"
#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/EmitTable.hpp"
#include "cursiv/shared/gen/HeadMatcher.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/LexicalPredicateEmitter.hpp"
#include "cursiv/shared/gen/SetPool.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

namespace {

std::vector<std::uint8_t> lr_guarded_columns(const LrBuildTable& table) {
  std::vector<std::uint8_t> guarded(table.cfg.nnts, 0);
  for (const auto& e : table.gotoRows.edges)
    if (e.guard != 0) guarded[e.nt] = 1;
  return guarded;
}

struct LrTableEmitter : TextBuilder {
  explicit LrTableEmitter(const LrFinalTable& done)
      : a(done.table()),
        guarded(lr_guarded_columns(a)),
        programs(done.programs()),
        names(a.cfg, std::span(a.relays.data(), a.nrelays)) {
    measure();
  }

  const LrBuildTable& a;
  const std::vector<std::uint8_t> guarded;
  bool aggregate = false;
  const LrProgramLayout& programs;

  const LexNamePool names;
  // The extent of every array, and its fields as `kSizes` spells them.
  LrSizes S{};
  std::string sizes{};

  // One extent, kept in `S` and spelled where it is not zero.
  void extent(unsigned LrSizes::* field, std::string_view name, std::size_t n) {
    S.*field = static_cast<unsigned>(n);
    if (n != 0) sizes += concat(sizes.empty() ? "." : ",.", name, "=", n);
  }
  // Every extent, in the order `LrSizes` declares them.
  void measure() {
    const auto& c = a.cfg;
    const std::size_t bounds = a.accBound.size();
    extent(&LrSizes::terms, "terms", c.nterms);
    extent(&LrSizes::prods, "prods", c.nprods);
    extent(&LrSizes::states, "states", a.nstates);
    extent(&LrSizes::modes, "modes", a.nmodes);
    extent(&LrSizes::cls, "cls", a.ncls);
    extent(&LrSizes::dfa, "dfa", a.ndfa);
    extent(&LrSizes::dfaAccSets, "dfaAccSets", a.dfaAccPool.sets.size());
    extent(&LrSizes::dfaResSets, "dfaResSets", a.dfaResPool.sets.size());
    extent(&LrSizes::modeWinSets, "modeWinSets", a.modeWinPool.sets.size());
    extent(&LrSizes::modeLongers, "modeLongers",
           a.longer_modes() ? a.nmodes : 0);
    extent(&LrSizes::modeBacktracks, "modeBacktracks", a.modeBacktrack.size());
    extent(&LrSizes::modeFlagSets, "modeFlagSets", a.modeFlagPool.sets.size());
    extent(&LrSizes::accBounds, "accBounds", bounds);
    extent(&LrSizes::relays, "relays", a.nrelays);
    extent(&LrSizes::followSets, "followSets", a.followPool.sets.size());
    extent(&LrSizes::closeSets, "closeSets", a.closePool.sets.size());
    extent(&LrSizes::dfaRelayResSets, "dfaRelayResSets",
           a.relayReserves() ? a.dfaRelayResPool.sets.size() : 0);
    extent(&LrSizes::accStands, "accStands", a.standing() ? bounds : 0);
    extent(&LrSizes::accPriorities, "accPriorities",
           a.prioritized() ? bounds : 0);
    extent(&LrSizes::errModes, "errModes", a.errModeOf.size());
    extent(&LrSizes::endModes, "endModes", a.modeEnd.size());
    extent(&LrSizes::insertLists, "insertLists", a.insertAt.size() - 1);
    extent(&LrSizes::insertWords, "insertWords", a.insertWord.size());
    extent(&LrSizes::electWords, "electWords", a.electWord.size());
    extent(&LrSizes::electWatches, "electWatches", a.electWatch.size());
    extent(&LrSizes::electChains, "electChains", a.electChainKey.size());
    extent(&LrSizes::electChainWords, "electChainWords",
           a.electChainWord.size());
    extent(&LrSizes::electTwins, "electTwins", a.electTwinFirst.size());
    extent(&LrSizes::electRhsSyms, "electRhsSyms", a.electRhs.size());
    extent(&LrSizes::orderWords, "orderWords", programs.exact.size());
    extent(&LrSizes::nameChars, "nameChars", names.bytes.size());
    extent(&LrSizes::sets, "sets", c.nsets);
    extent(&LrSizes::wordRanges, "wordRanges", c.wordRanges.size());
    extent(&LrSizes::scalarRanges, "scalarRanges", c.scalarRanges.size());
    extent(&LrSizes::lexElseRows, "lexElseRows", a.lexElseRows.size());
    extent(&LrSizes::typoSucc, "typoSucc", a.typoSucc.size());
    extent(&LrSizes::reduceNames, "reduceNames", a.reduceNames.size());
    extent(&LrSizes::expectItems, "expectItems", a.expectItems.size());
    extent(&LrSizes::expectPool, "expectPool", a.expectPool.size());
    extent(&LrSizes::rowLhsRows, "rowLhsRows",
           a.rowLhsBase.empty() ? 0 : a.rowLhsBase.size() - 1);
    extent(&LrSizes::rowLhs, "rowLhs", a.rowLhs.size());
    extent(&LrSizes::typoReachSets, "typoReachSets", a.typoReach.sets.size());
    S.indented = a.indented();
    if (S.indented) sizes += ",.indented=true";
    S.gotoClasses = a.nconflicts != 0 || a.multiCand;
    if (!S.gotoClasses) sizes += ",.gotoClasses=false";
  }

  void u32(unsigned long long v) { write(v, "u"); }
  void u64(unsigned long long v) { write(v, "ull"); }

  void cell(char c) { write("char(", static_cast<unsigned char>(c), ")"); }
  void cell(std::uint8_t v) { write(v); }
  void cell(std::uint16_t v) { write(v); }
  void cell(std::uint32_t v) { u32(v); }
  void cell(std::uint64_t v) { u64(v); }
  void cell(const ByteSet& s) {
    write("{{");
    for (unsigned k = 0; k < 4; ++k) {
      if (k) write(",");
      u64(s.w[k]);
    }
    write("}}");
  }
  void cell(const LrLexElse& e) {
    write("{.bytes=");
    cell(e.bytes);
    write(",.term=", e.term);
    if (e.scalar != 0) write(",.scalar=", e.scalar);
    write("}");
  }
  // A terminal set at the width the frozen table holds.
  void cell(const DynSet& s) {
    const unsigned words = (S.termBits() + 63u) / 64u;
    write("{{");
    for (unsigned k = 0; k < words; ++k) {
      if (k) write(",");
      u64(s.word(k));
    }
    write("}}");
  }
  void cell(const EmitFields& f) { write("{", f.list(), "}"); }
  void cell(const TermInfo& t) {
    constexpr TermInfo d{};
    EmitFields f;
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
    f("synthetic", t.synthetic, d.synthetic);
    f("cond", t.cond, d.cond);
    f("refuses", t.refuses, d.refuses);
    cell(f);
  }
  void cell(const LrProduction& p) {
    constexpr LrProduction d{};
    EmitFields f;
    f("lhs", p.lhs, d.lhs);
    f("node", p.node, d.node);
    f("len", p.len, d.len);
    f("gap", p.gap, d.gap);
    f("guarded", p.guarded, d.guarded);
    cell(f);
  }
  void cell(const LrReducePlan& p) {
    constexpr LrReducePlan d{};
    EmitFields f;
    f("names", p.names, d.names, "", "u");
    f("count", p.count, d.count);
    f("form", static_cast<std::uint8_t>(p.form),
      static_cast<std::uint8_t>(d.form), "::cursiv::engine::LrReduceForm(",
      ")");
    f("flushes", p.flushes, d.flushes);
    f("turnRow", p.turnRow, d.turnRow);
    f("regionExit", p.regionExit, d.regionExit);
    cell(f);
  }
  void cell(const LrReduceName& n) {
    constexpr LrReduceName d{};
    EmitFields f;
    f("slot", n.slot, d.slot);
    f("row", n.row, d.row);
    f("fold", n.fold, d.fold);
    f("leaf", n.leaf, d.leaf);
    cell(f);
  }
  void cell(const LexRelayGen& r) { write(lex_relay_source(r, names)); }
  void cell(const unicode::Range& r) {
    write("{", static_cast<std::uint32_t>(r.lo), "u,",
          static_cast<std::uint32_t>(r.hi), "u}");
  }
  void cell(const LrExpect& e) {
    write("{.feature=", e.feature, ",.site=", e.site, ",.toState=", e.toState,
          ",.entered=", e.entered, "}");
  }

  // The first `n` cells of `src`, sixteen a line.
  template <class Src>
  void cells(const Src& src, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      if (i) write(",");
      if (i % 16 == 0) write("\n    ");
      cell(src[i]);
    }
  }
  // `member = {{cells}};`: `n` cells of the source, the rest defaulted.
  template <class Src>
  void array(std::string_view member, const Src& src, std::size_t n,
             std::size_t have) {
    std::size_t count = std::min(n, have);
    using Cell = std::remove_cvref_t<decltype(src[0])>;
    if constexpr (std::is_integral_v<Cell> && !std::is_same_v<Cell, char>) {
      while (count != 0 && src[count - 1] == 0) --count;
      if (count == 0) return;
    }
    write(aggregate ? "  ." : "  t.");
    write(member);
    if (count == 0) {
      line(aggregate ? " = {}," : " = {};");
      return;
    }
    write(" = {{");
    cells(src, count);
    line(aggregate ? "}}," : "}};");
  }
  template <class Src>
  void array(std::string_view member, const Src& src,
             std::size_t n = ~std::size_t{0}) {
    array(member, src, n, src.size());
  }

  void emit_cold() {
    aggregate = true;
    line("inline constexpr ::cursiv::engine::LrColdTables<kSizes> kCold{");
    emit_pool_member("follow", a.followPool);
    array("accBoundBase", a.accBoundBase, S.dfa + 1u);
    array("accBound", a.accBound, S.accBounds);
    if (S.accStands != 0) array("accStand", a.accStand, S.accStands);
    if (S.accPriorities != 0)
      array("accPriority", a.accPriority, S.accPriorities);
    array("soleReduce", a.soleReduce, S.states);
    array("readsToken", a.readsToken, S.states);
    if (S.gotoClasses) array("gotoClass", a.gotoClass, S.states);
    emit_pool_member("close", a.closePool);
    line("  .closeMax = ", a.closeMax, "u,");
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
      array("electTwinFirst", a.electTwinFirst);
      array("electTwinNext", a.electTwinNext);
      array("electRhsAt", a.electRhsAt);
      array("electRhs", a.electRhs);
    }
    line("};");
    aggregate = false;
  }

  void emit_facts() {
    line(
        "  static_cast<::cursiv::engine::LrTableFacts&>(t) = "
        "::cursiv::engine::LrTableFacts{");
    write("    .nstates=", a.nstates, ",.nconflicts=", a.nconflicts,
          ",.npeeks=", a.npeeks,
          ","
          "\n    .cls={");
    for (unsigned b = 0; b < 256; ++b) write(b ? "," : "", a.cls[b]);
    write("},\n    .ncls=", a.ncls, ",.multiCand=", a.multiCand,
          ","
          "\n    .errMode=",
          a.errMode, ",.maxCands=", a.maxCands,
          ","
          ".maxArms=",
          a.maxArms);
    if (a.stamp != StampLayout{})
      write(",\n    .stamp={.rowBits=", a.stamp.rowBits, "}");
    line("};");
  }

  void emit_cfg() {
    const auto& c = a.cfg;
    line(
        "  static_cast<::cursiv::engine::LrLowFacts&>(t.cfg) = "
        "::cursiv::engine::LrLowFacts{");
    write("    .nterms=", c.nterms, ",.nnts=", c.nnts, ",.nprods=", c.nprods,
          ",.entryRow=", c.entryRow, ",.wordBytes=");
    cell(c.wordBytes);
    line("};");
    array("cfg.names", names.bytes, S.nameChars);
    array("cfg.sets", c.sets, S.sets);
    array("cfg.setDecode", c.setDecode, S.sets);
    array("cfg.terms", c.terms, S.terms);
    std::vector<LrProduction> prods(c.nprods);
    for (unsigned p = 0; p < c.nprods; ++p)
      prods[p] = {c.prods[p].lhs, c.prods[p].node, c.prods[p].len,
                  c.prods[p].gap, guarded[c.prods[p].lhs]};
    array("cfg.prods", prods, S.prods);
    array("cfg.wordRanges", c.wordRanges, S.wordRanges);
    if (S.scalarRanges != 0) {
      std::vector<std::uint32_t> scalars(c.nsets);
      for (std::uint16_t i = 0; i < c.nsets; ++i)
        scalars[i] = lex_set_scalars(c, i);
      array("cfg.setScalars", scalars);
      array("cfg.scalarRanges", c.scalarRanges, S.scalarRanges);
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
    if (S.errModes != 0) array("errModeOf", a.errModeOf, S.errModes);
    if (S.endModes != 0) {
      array("modeText", a.modeText, S.endModes);
      array("modeEnd", a.modeEnd, S.endModes);
    }
    if (!S.indented) array("modeSub", a.modeSub, S.modes * a.mode_sub_words());
    line("  t.dfaNext = {{");
    text_rows(*this, {a.dfaNext.data(), std::size_t{S.dfa} * S.cls}, S.cls);
    line("  }};");
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
    line(
        "inline constexpr ::cursiv::engine::LrDerivedTables<kSizes> kDerived{");
    aggregate = true;
    array("skipSet", a.skipSet);
    array("skipStop", a.skipStop);
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
    array("preds", a.preds);
    emit_pool_member("typoReach", a.typoReach);
    array("lexBodies", a.lexBodies);
    array("lexBodyTerms", a.lexBodyTerms);
    array("lexOpeners", a.lexOpeners);
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
    line("  .", name, " = {");
    emit_pool("pool", "of", set);
    line("  },");
  }

  // An array a guard site reads its own cells of: every unit sees them.
  template <class Values>
  void guard_array(std::string_view type, std::string_view name,
                   const Values& values) {
    write("inline constexpr ", type, " ", name, "[] = {");
    cells(values, values.size());
    if (values.empty()) write("{}");
    line("};");
  }

  // The definitions of the programs' arrays, which one unit holds for all.
  std::string programData{};

  // An array of the programs: declared to every unit, its definition kept
  // for the end of the header.
  template <class Values>
  void program_array(std::string_view type, std::string_view name,
                     const Values& values) {
    const auto decl = concat("const ", type, " ", name, "[",
                             std::max<std::size_t>(values.size(), 1), "]");
    line("extern ", decl, ";");
    const std::size_t at = size();
    write(decl, " = {");
    cells(values, values.size());
    line("};");
    programData += cut_from(at);
  }

  void program_matrix(const LrProgramMatrix& matrix, std::string_view name,
                      std::string_view value, unsigned columns,
                      unsigned absent) {
    const auto row = concat("::std::uint", matrix.row_bits(), "_t");
    const auto index = concat("::std::uint", matrix.index_bits(), "_t");
    program_array(row, concat(name, "Rows"), matrix.rows);
    program_array(index, concat(name, "Blocks"), matrix.blocks);
    program_array(value, concat(name, "Values"), matrix.values);
    line("inline constexpr ::cursiv::engine::LrProgramRows<", row, ", ", index,
         ", ", value, "> ", name, "{\n    ", name, "Rows, ", name, "Blocks, ",
         name, "Values, ", columns, "u, ", absent, "u};");
  }

  void emit_programs() {
    const auto& p = programs;
    const auto value = concat("::std::uint", p.actions.value_bits(), "_t");
    program_matrix(p.actions, "kActionMatrix", value, S.terms, 0);
    program_matrix(p.gotos, "kGotoPrograms", "::std::uint16_t", a.cfg.nnts,
                   0xffffu);
    const std::uint32_t most =
        p.ops.empty() ? 0 : *std::ranges::max_element(p.ops);
    const auto op = concat("::std::uint", LrProgramMatrix::bits(most), "_t");
    program_array(op, "kProgramOps", p.ops);
    line("inline constexpr ::cursiv::engine::LrActionPrograms<", "::std::uint",
         p.actions.row_bits(), "_t", ", ", "::std::uint",
         p.actions.index_bits(), "_t", ", ", value, ", ", op,
         "> "
         "kPrograms{\n"
         "    kActionMatrix, kProgramOps, ",
         p.countBits, "u};");
  }

  // Guard `k` stores at `kGuardAt[k]` its condition count, the conditions and a
  // target per outcome; `kHeld` holds per condition a bit per state, set
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
    guard_array("::std::uint32_t", "kGuardAt", at);
    guard_array("::std::uint16_t", "kGuardOps", ops);
    const std::size_t words = (std::size_t{a.nstates} + 63u) / 64u;
    std::vector<std::uint64_t> held(words * a.cfg.ngapConds, 0);
    for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
      for (unsigned s = 0; s < a.nstates; ++s)
        if (!a.tagFalse[s].test(k))
          held[k * words + s / 64u] |= std::uint64_t{1} << (s % 64u);
    guard_array("::std::uint64_t", "kHeld", held);
  }

  static bool any_words(const std::vector<HeadMatcher>& heads) {
    return std::ranges::any_of(
        heads, [](const HeadMatcher& h) { return !h.words.empty(); });
  }
  bool refusing() const { return any_words(a.termHeads); }
  bool trailing() const { return any_words(a.termTails); }

  void emit(std::string_view ns) {
    write(R"(// GENERATED by the native table emitter -- do not edit.
// The frozen table of one grammar, as data: the engine's whole reading of it.
#pragma once
#include <cstdint>

#include "cursiv/lr/table/LrColdTables.hpp"
#include "cursiv/lr/table/LrDerivedTables.hpp"
#include "cursiv/lr/table/LrFrozenTable.hpp"

)");
    line("#include \"cursiv/lr/table/LrActionPrograms.hpp\"");
    if (refusing()) {
      line("#include <algorithm>");
      line("#include \"cursiv/utils/Utf8.hpp\"");
      line("#include \"cursiv/utils/macros.hpp\"");
      line("#include \"cursiv/shared/runtime/lex_scalar_in.hpp\"");
    }
    if (!a.guards.empty()) line("#include <span>");
    line("namespace ", ns, " {\n");
    write(LexicalPredicateEmitter(a.cfg,
                                  ::std::span(a.relays.data(), a.nrelays), "")
              .declarations());
    line("inline constexpr ::cursiv::engine::LrSizes kSizes{", sizes, "};");
    emit_cold();
    emit_derived();
    emit_programs();
    emit_guards();
    line(
        "struct Table : ::cursiv::engine::LrFrozenTable<kSizes, kCold, "
        "kDerived> {\n"
        "  static constexpr auto& programs = kPrograms;\n"
        "  static constexpr auto& gotoPrograms = kGotoPrograms;");
    if (!a.guards.empty())
      line(
          "  static constexpr ::std::span<const ::std::uint32_t> guardAt{"
          "kGuardAt};\n"
          "  static constexpr ::std::span<const ::std::uint16_t> guardOps{"
          "kGuardOps};\n"
          "  static constexpr ::std::span<const ::std::uint64_t> held{kHeld};");
    if (refusing())
      line(
          "  static bool head_refuses(::cursiv::engine::SymbolId t, const "
          "char* p,\n"
          "                           ::cursiv::TextOffset rest);\n"
          "  static ::cursiv::TextOffset head_reach(unsigned m, const char* "
          "p,\n"
          "                                         ::cursiv::TextOffset "
          "rest);");
    if (const auto words = lex_word_bounds(a.cfg);
        !words.empty() && (refusing() || a.cfg.sections())) {
      write("  static constexpr ::std::uint16_t kWordBounds[] = {", words[0],
            "u");
      for (std::size_t i = 1; i < words.size(); ++i) write(", ", words[i], "u");
      line(
          "};\n"
          "  static constexpr ::std::span<const ::std::uint16_t> wordBounds{"
          "kWordBounds};");
    }
    if (trailing())
      line(
          "  static bool tail_refuses(::cursiv::engine::SymbolId t, const "
          "char* p,\n"
          "                           ::cursiv::TextOffset rest);");
    if (a.nrelays != 0)
      line(
          "  static ::cursiv::TextOffset relay_run(::std::uint16_t, const "
          "char*,\n"
          "      ::cursiv::TextOffset, ::cursiv::TextOffset);");
    line("};");
    emit_automaton();
    if (refusing())
      write(lex_head_source(
          a.termHeads, a.modeHeads,
          "inline bool Table::head_refuses(::cursiv::engine::SymbolId t, "
          "const char* p,\n    ::cursiv::TextOffset rest)",
          "inline ::cursiv::TextOffset Table::head_reach(unsigned m,\n"
          "    [[maybe_unused]] const char* p,\n"
          "    [[maybe_unused]] ::cursiv::TextOffset rest)",
          {"kTable.cfg.wordRanges.data(), kTable.cfg.wordRanges.size()",
           "kTable.cfg.scalarRanges.data()"}));
    if (trailing())
      write(lex_tail_source(
          a.termTails,
          "inline bool Table::tail_refuses(::cursiv::engine::SymbolId t, "
          "const char* p,\n    ::cursiv::TextOffset rest)",
          {"kTable.cfg.wordRanges.data(), kTable.cfg.wordRanges.size()",
           "kTable.cfg.scalarRanges.data()"}));
    line("\n}  // namespace ", ns);
    // The unit that defines `CURSIV_TABLE_DATA` holds the programs.
    line("\n#ifdef CURSIV_TABLE_DATA\nnamespace ", ns, " {");
    write(programData);
    line("}  // namespace ", ns, "\n#endif");
  }
};

}  // namespace

std::string lr_emit_table(const LrFinalTable& done, std::string_view ns) {
  LrTableEmitter e{done};
  e.reserve(1u << 20);
  e.emit(ns);
  return e.take();
}

}  // namespace cursiv::engine
