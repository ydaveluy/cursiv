// The table as source: every array the runtime reads, and the one view that
// names them. Nothing here is computed twice -- what the construction settled
// is printed, and what a reader derives is printed too, so no module folds a
// question out of the table at load time. The functions the view points at are
// declared beside it and defined once, in the parser's own unit.
#include "cursiv/engines/ll/gen/ll_emit_table.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engines/ll/gen/LlExitReads.hpp"
#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"
#include "cursiv/engines/ll/gen/LlLexView.hpp"
#include "cursiv/engines/shared/gen/CLiteral.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/GapConditionEmitter.hpp"
#include "cursiv/engines/shared/gen/GapEmitter.hpp"
#include "cursiv/engines/shared/gen/GapReach.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexFirstByte.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LexicalPredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/NamedHead.hpp"
#include "cursiv/engines/shared/gen/PredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"

namespace cursiv {
namespace {

using Built = LlBuiltProgram;

std::string u(std::uint64_t v) { return std::format("{}u", v); }
std::string byte_set(const ByteSet& s) {
  return std::format("{{{{{}u,{}u,{}u,{}u}}}}", s.w[0], s.w[1], s.w[2], s.w[3]);
}
std::string range(const unicode::Range& r) {
  return std::format("{{{}u,{}u}}", static_cast<std::uint32_t>(r.lo),
                     static_cast<std::uint32_t>(r.hi));
}

struct Out : EmitOutput {
  // The arrays left out, empty or not wanted: a reference to one is null.
  std::vector<std::string_view> emptyArrays;

  // One array, wrapped: a line per twelve entries keeps the generated file
  // readable and the compiler's own line table short.
  template <class V, class F>
  void array(std::string_view type, std::string_view name, const V& v,
             F&& one) {
    if (v.empty()) {
      emptyArrays.push_back(name);
      return;
    }
    fmt("inline constexpr {} {}[] = {{", type, name);
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (i % 12 == 0) out += "\n    ";
      out += one(v[i]);
      if (i + 1 != v.size()) out += ',';
    }
    line("};");
  }

  template <class V>
  void array(std::string_view type, std::string_view name, const V& v) {
    array(type, name, v, u);
  }

  void omit(std::string_view name) { emptyArrays.push_back(name); }

  std::string_view ref(std::string_view name) const {
    return std::ranges::find(emptyArrays, name) == emptyArrays.end()
               ? name
               : "nullptr";
  }
  void ptr(std::string_view field, std::string_view name) {
    if (ref(name) != "nullptr") fmt("    .{} = {},\n", field, name);
  }
};

// A terminal set per state, pooled: the distinct sets at the width this
// grammar needs (the construction's sets grow, a frozen row is a fixed run of
// words), and per state the word its row starts at -- an offset, not a row
// number, so a reader adds where it would have multiplied.
struct TermRows {
  std::vector<std::uint64_t> pool;
  std::vector<std::uint32_t> at;

  TermRows(std::span<const DynSet> src, std::uint32_t words) {
    const SetPool rows(src.data(), static_cast<unsigned>(src.size()));
    for (const DynSet& set : rows.sets)
      for (std::uint32_t k = 0; k < words; ++k) pool.push_back(set.word(k));
    for (const std::uint16_t row : rows.of) at.push_back(row * words);
  }
};

// Per state, what a walk can still accept from it on, itself included: what
// it and every state it leads to accept or relay.
std::vector<DynSet> lex_live(const Built& a) {
  std::vector<DynSet> live(a.dfaAccSet.data(), a.dfaAccSet.data() + a.ndfa);
  std::vector<std::vector<std::uint32_t>> into(a.ndfa);
  for (std::uint32_t s = 0; s < a.ndfa; ++s) {
    if (!a.relays.empty())
      for (auto k = a.dfaRelay[s]; k != kNoRelay; k = a.relays[k].next)
        live[s].set(a.relays[k].term);
    for (unsigned c = 0; c < a.ncls; ++c) {
      const auto to = a.dfaNext[std::size_t{s} * a.ncls + c];
      if (to != kDeadState && to != s &&
          (into[to].empty() || into[to].back() != s))
        into[to].push_back(s);
    }
  }
  std::vector<std::uint32_t> todo(a.ndfa);
  for (std::uint32_t s = 0; s < a.ndfa; ++s) todo[s] = s;
  while (!todo.empty()) {
    const std::uint32_t s = todo.back();
    todo.pop_back();
    for (const std::uint32_t from : into[s])
      if (live[from].add(live[s])) todo.push_back(from);
  }
  return live;
}

// Whether a state of the automaton has a terminal in `sets`.
bool any_set(const Built& a, const Vec<DynSet>& sets) {
  return sets.size() >= a.ndfa &&
         std::any_of(sets.data(), sets.data() + a.ndfa,
                     [](const DynSet& set) { return set.any(); });
}

// Per mode, the one-byte literal each first byte reads whole: rows of 256,
// one per distinct row, and per mode where its row starts. Empty where no
// mode reads one.
struct ByteTerms {
  std::vector<std::uint16_t> rows;
  std::vector<std::uint32_t> at;

  explicit ByteTerms(const Built& a) {
    std::vector<unsigned> spelled;
    for (unsigned t = 1; t < a.cfg.nterms; ++t)
      if (lex_byte_spelled(LlLexView{a}, t)) spelled.push_back(t);
    if (spelled.empty()) return;
    std::map<std::vector<std::uint16_t>, std::uint32_t> seen;
    bool any = false;
    for (unsigned m = 0; m < a.nmodes; ++m) {
      std::vector row(256, kInvalidSymbol);
      for (const unsigned t : spelled)
        if (ll_byte_term(a, m, t)) {
          row[static_cast<unsigned char>(
              a.cfg.term_text(static_cast<std::uint16_t>(t))[0])] =
              static_cast<std::uint16_t>(t);
          any = true;
        }
      const auto [it, fresh] =
          seen.try_emplace(row, static_cast<std::uint32_t>(rows.size()));
      if (fresh) rows.insert(rows.end(), row.begin(), row.end());
      at.push_back(it->second);
    }
    if (!any) {
      rows.clear();
      at.clear();
    }
  }
};

std::string staged_source(const Built& a) {
  Out o;
  std::vector<bool> live(a.dfa.size());
  std::vector<bool> entry(a.dfa.size()), reads(a.dfa.size());
  std::vector<std::uint32_t> pending;
  for (const auto& dec : a.cfg.decisions.v)
    if (dec.how == LlPredict::staged) {
      entry[dec.dfa] = true;
      pending.push_back(dec.dfa);
    }
  for (std::size_t i = 0; i < pending.size(); ++i) {
    const auto at = pending[i];
    if (live[at]) continue;
    live[at] = true;
    const auto& state = a.dfa[at];
    if (state.alt != kNoRow) continue;
    for (std::uint32_t e = 0; e < state.nedges; ++e) {
      const auto target = a.dfaEdge[state.edge + e].target;
      reads[target] = true;
      pending.push_back(target);
    }
  }
  // The locals of a read past the token in hand go unused where no choice reads
  // one.
  bool ahead = false;
  for (std::size_t at = 0; at < live.size(); ++at)
    ahead = ahead || (live[at] && reads[at] && a.dfa[at].alt == kNoRow);
  const std::string_view unused = ahead ? "" : "[[maybe_unused]] ";
  o.line("::std::uint16_t ll_staged_predict(const ::cursiv::LlContext& c,");
  o.line("    ::std::uint16_t decision) {");
  o.fmt("  {}auto& indent = ::cursiv::ll_look_indent<kTable>(c);\n", unused);
  o.line(
      "  auto& scratch = ::cursiv::ll_scratch<kTable.termWords>();\n"
      "  auto& viable = scratch.viable;\n  auto& read = scratch.read;\n"
      "  const auto& dec = kTable.decisions[decision];");
  o.fmt("  {}const auto look = kTable.look_mode(dec);\n",
        a.lookMode.empty() && ahead ? "" : "[[maybe_unused]] ");
  o.fmt("  {}auto from = c.la.end;\n  auto sym = c.la.sym;\n", unused);
  o.fmt("  {}::cursiv::LlLookRead nextRead;\n", unused);
  o.line(
      "  if (::cursiv::ll_reads_rivals<kTable>(c, dec.mode)) {\n"
      "    ::cursiv::LlLookRead first;\n"
      "    ::cursiv::ll_first_read<kTable>(c, dec, viable, read, &first);\n"
      "    if (first.different || read.size() != 1) return ::cursiv::kNoRow;\n"
      "    sym = read.front();\n  }\n  switch (decision) {");
  for (std::uint16_t d = 0; d < a.cfg.ndecisions; ++d)
    if (const auto& dec = a.cfg.decisions[d]; dec.how == LlPredict::staged)
      o.fmt("    case {}u: goto S{};\n", d, dec.dfa);
  o.line("    default: return ::cursiv::kNoRow;\n  }");
  for (std::size_t at = 0; at < live.size(); ++at) {
    if (!live[at]) continue;
    const auto& state = a.dfa[at];
    if (reads[at]) o.fmt("R{}:\n", at);
    if (reads[at] && state.alt == kNoRow) {
      o.line("  ++::cursiv::ll_staged_reads<kTable>();\n  viable.reset();");
      for (std::uint32_t e = 0; e < state.nedges; ++e)
        o.fmt("  viable.set({}u);\n", a.dfaEdge[state.edge + e].term);
      // The next token is crossed under the configuration of the position the
      // readings stand before.
      const std::uint16_t own =
          state.node != kNoRow ? a.cfg.nodes[state.node].mode : kNoRow;
      o.fmt(
          "  if (!::cursiv::ll_look<kTable>(c, from, viable, read, {},\n"
          "          &nextRead, &indent) || read.size() != 1 || "
          "nextRead.different)\n"
          "    return ::cursiv::kNoRow;\n  sym = read.front();\n",
          own != kNoRow ? std::format("{}u", own) : "look");
    }
    if (entry[at]) o.fmt("S{}:\n", at);
    if (state.alt != kNoRow) {
      o.fmt("  return {}u;\n", state.alt);
    } else {
      o.line("  switch (sym) {");
      for (std::uint32_t e = 0; e < state.nedges; ++e) {
        const auto& edge = a.dfaEdge[state.edge + e];
        o.fmt("    case {}u: goto R{};\n", edge.term, edge.target);
      }
      o.line("    default: return ::cursiv::kNoRow;\n  }");
    }
  }
  o.line("}");
  return o.out;
}

}  // namespace

LlTableSource ll_emit_table(const LlFinalProgram& done, const std::string& ns) {
  const Built& a = done.program();
  const LlStampPlan& plan = done.plan();
  // The header, and the functions it declares.
  Out o, fn;
  const auto& g = a.cfg;
  const GapEmitter gaps(g, "kSets", "kGapBoundary", "inline");
  const PredicateEmitter predicates(g, gaps, a.sectionErr);
  const LexicalPredicateEmitter lexical(g, a.relays,
                                        "::cursiv::lex_boundary(kTable)");
  const bool compiledPredicates = std::ranges::any_of(
      g.gapLeaves.v, [](const GapLeaf& leaf) { return leaf.reads_tokens(); });
  const bool lexChecks = std::ranges::any_of(
      a.lexChecks.v, [](std::uint8_t checks) { return checks != 0; });
  const bool staged = std::ranges::any_of(
      g.decisions.v,
      [](const LlDecision& d) { return d.how == LlPredict::staged; });
  const bool flagging = std::ranges::any_of(
      a.decFlag, [](const DynSet& set) { return set.any(); });
  // A climb asks where it ends, and a flagged choice whether a caller takes
  // its token: both read the frames the descent stands in.
  const bool callers = g.nclimbs != 0 || flagging;
  const bool reserving = any_set(a, a.dfaResSet);
  const bool relayReserving = any_set(a, a.dfaRelayResSet);
  // With no `joins` terminal the modes' joined trivia is their plain trivia.
  const bool joinedApart = a.modeJoinedTrivia != a.modeTrivia;
  const bool longer = std::ranges::any_of(
      a.modeLonger, [](std::uint16_t m) { return m != kNoMode; });
  const std::uint32_t words = (g.nterms + 63u) / 64u;
  const std::vector<std::uint64_t> skipSet = lex_skip_set(LlLexView{a});
  const ByteTerms byteTerms(a);
  const bool classes = g.scalarRanges.size() != 0;
  // A boundary reads `kSetDecode` only where the word class has ranges or a
  // set has scalars of its own.
  const bool decoding = g.wordRanges.size() != 0 || classes;
  const LexNamePool pool(g, a.relays);
  const auto tabled = [](const auto& bases, bool rides) {
    for (std::size_t i = 1; i < bases.size(); ++i)
      if (ll_fuse_tabled(bases[i] - bases[i - 1], rides)) return true;
    return false;
  };
  bool fuseTable = tabled(a.ownBase, true) || tabled(a.fuseBase, false);
  for (std::uint16_t i = 0; !fuseTable && i < g.nclimbs; ++i)
    fuseTable = ll_fuse_tabled(g.climbs[i].leftCount, false) ||
                ll_fuse_tabled(g.climbs[i].rightCount, false);
  o.line("// GENERATED by the predictive engine -- do not edit.");
  o.line("#pragma once");
  o.line("");
  o.line("#include <algorithm>");
  o.line("#include <cstdint>");
  o.line("#include <utility>");
  o.line("");
  o.line("#include \"cursiv/engines/ll/table/LlTable.hpp\"");
  o.line("#include \"cursiv/engines/shared/runtime/Scan.hpp\"");
  if (fuseTable) o.line("#include \"cursiv/engines/ll/table/LlFuse.hpp\"");
  o.line("#include \"cursiv/core/utils/Utf8.hpp\"");
  o.line("#include \"cursiv/core/utils/macros.hpp\"");
  o.line("#include \"cursiv/engines/shared/runtime/lex_scalar_in.hpp\"");
  o.line("");
  if (compiledPredicates) {
    o.line("#include \"cursiv/engines/ll/runtime/ll_matcher.hpp\"");
    o.line("#include \"cursiv/engines/shared/runtime/LexSuffixSession.hpp\"");
  }
  if (compiledPredicates || lexical.any() || g.ngapConds != 0)
    o.line("#include \"cursiv/engines/shared/runtime/LexBoundary.hpp\"");
  if (compiledPredicates || lexical.any())
    o.line("#include \"cursiv/engines/shared/runtime/PredicateState.hpp\"");
  if (g.ngapConds != 0 || lexical.any())
    o.line("#include \"cursiv/engines/shared/runtime/GapPositions.hpp\"");
  // What only the functions defined in the parser's own unit read.
  std::string includes;
  if (staged)
    includes += "#include \"cursiv/engines/ll/runtime/ll_predictor.hpp\"\n";
  if (lexical.any())
    includes +=
        "#include <exception>\n"
        "#include \"cursiv/engines/shared/runtime/LexicalMachine.hpp\"\n";
  if (lexical.searches())
    includes += "#include \"cursiv/engines/shared/runtime/lex_find.hpp\"\n";
  o.fmt("namespace {} {{\n", ns);
  o.line("");
  if (staged)
    o.line(
        "::std::uint16_t ll_staged_predict(const ::cursiv::LlContext&, "
        "::std::uint16_t);");
  o.line(lexical.declarations());
  if (pool.bytes.empty()) {
    o.omit("kNames");
  } else {
    o.line("inline constexpr char kNames[] =");
    const std::string_view names(pool.bytes.data(), pool.bytes.size());
    for (std::size_t at = 0; at < names.size(); at += 60)
      o.fmt("    {}\n", c_literal(names.substr(at, 60)));
    o.line(";");
  }

  o.array("::cursiv::ByteSet", "kSets", std::span(g.sets.data(), g.nsets),
          byte_set);
  if (decoding)
    o.array("::std::uint16_t", "kSetDecode", g.setDecode.v);
  else
    o.omit("kSetDecode");
  o.array("::cursiv::TermInfo", "kTerms", std::span(g.terms.data(), g.nterms),
          [&](const TermInfo& x) {
            return std::format(
                "{{{}u,{}u,{}u,{},{},{},{}u,{}u,{},{},{}u,{}u,{}}}",
                pool(x.nameOff), x.nameLen, x.element, x.trivia, x.recorded,
                x.spelled, pool(x.textOff), x.textLen, x.delim, x.joins, x.laid,
                x.cond, x.refuses);
          });
  // A climb reads its operator's token, and the gap behind it, under its own
  // alphabet, and so does a caret that reads that token again.
  std::vector opMode(g.nnodes, kNoRow);
  for (std::uint16_t k = 0; k < g.nclimbs; ++k)
    for (std::uint16_t i = 0; i < g.climbs[k].opCount; ++i)
      if (const auto leaf =
              g.atomic_operator(g.climbOps[g.climbs[k].opBase + i].body);
          leaf != kNoRow)
        opMode[leaf] = g.climbs[k].mode;
  o.array("::cursiv::LlTable::Node", "kNodes",
          std::span(g.nodes.data(), g.nnodes), [&](const LlNode& x) {
            const auto id = static_cast<std::size_t>(&x - g.nodes.data());
            const auto mode =
                opMode[id] != kNoRow && a.nodeAfter[id] != 0xFFFFFFFFu
                    ? opMode[id]
                    : x.mode;
            return std::format(
                "{{{}u,{}u,{}u,{},{}u}}", x.row, mode,
                x.kind == LlShape::term || x.kind == LlShape::implied ? x.sym
                                                                      : kNoRow,
                a.nodeHead[id] != 0,
                plan.covers(static_cast<std::uint16_t>(id)));
          });
  // A climb's choice of operator is the decision its select position makes.
  const auto again = [&](const LlDecision& x) {
    if (x.rule == kNoRow || g.rules[x.rule].climb == kNoRow) return false;
    const std::uint16_t select = g.climbs[g.rules[x.rule].climb].selectNode;
    return select != kNoRow &&
           g.nodes[select].decision == &x - g.decisions.data();
  };
  o.array(
      "::cursiv::LlTable::Decision", "kDecisions",
      std::span(g.decisions.data(), g.ndecisions), [&](const LlDecision& x) {
        return std::format("{{{},::cursiv::LlPredict({}),{}u,{}u}}", again(x),
                           static_cast<unsigned>(x.how), x.mode, x.state);
      });
  o.array("::std::uint8_t", "kCls", std::span(a.cls));
  o.array("::std::uint16_t", "kModeStart",
          std::span(a.modeStart.data(), a.nmodes));
  o.array("::std::uint8_t", "kModeMulti",
          std::span(a.modeMulti.data(), a.nmodes));
  o.array("::std::uint8_t", "kModeBacktrack", a.modeBacktrack);
  if (longer) o.array("::std::uint16_t", "kModeLonger", a.modeLonger);
  o.array("::std::uint16_t", "kModeTrivia", a.modeTrivia);
  if (joinedApart)
    o.array("::std::uint16_t", "kModeJoinedTrivia", a.modeJoinedTrivia);
  o.array("::std::uint64_t", "kModeTerms", a.modeTerms);
  o.array("::std::uint64_t", "kModeSub", a.modeSub);
  if (std::ranges::any_of(skipSet, [](std::uint64_t w) { return w != 0; }))
    o.array("::std::uint64_t", "kSkipSet", skipSet);
  else
    o.omit("kSkipSet");
  o.array("::std::uint16_t", "kByteTerms", byteTerms.rows);
  o.array("::std::uint32_t", "kModeBytes", byteTerms.at);
  o.array("::std::uint16_t", "kLexNext",
          std::span(a.dfaNext.data(), std::size_t{a.ndfa} * a.ncls));
  o.array("::std::uint32_t", "kTypoSuccBase", a.typoSuccBase);
  o.array("::std::uint16_t", "kTypoSucc", a.typoSucc);
  o.array("::std::uint8_t", "kLexBodies", a.lexBodies);
  o.array("::std::uint16_t", "kLexOwed", a.lexOwed);
  o.array("::cursiv::ByteSet", "kLexOpeners", a.lexOpeners, byte_set);
  o.array("::std::uint16_t", "kLexAccept",
          std::span(a.dfaAccept.data(), a.ndfa));
  o.array("::std::uint16_t", "kLexBound", std::span(a.dfaBound.data(), a.ndfa));
  o.array("::std::uint32_t", "kLexChoiceAt", a.lexChoiceAt.v);
  o.array("::cursiv::LlLexAccept", "kLexChoices", a.lexChoices,
          [](const LlLexAccept& x) {
            return std::format("{{{}u,{}u,{}u}}", x.term, x.bound, x.tier);
          });
  if (lexChecks) o.array("::std::uint8_t", "kLexChecks", a.lexChecks.v);
  if (!a.relays.empty()) {
    o.array("::std::uint16_t", "kLexRelay",
            std::span(a.dfaRelay.data(), a.ndfa));
    o.array("::cursiv::LexRelay", "kRelays", a.relays,
            [&](const LexRelayGen& x) { return lex_relay_source(x, pool); });
  }
  const TermRows acc(std::span(a.dfaAccSet.data(), a.ndfa), words);
  o.array("::std::uint64_t", "kLexAcc", acc.pool);
  o.array("::std::uint32_t", "kLexAccAt", acc.at);
  const TermRows live(lex_live(a), words);
  o.array("::std::uint64_t", "kLexLive", live.pool);
  o.array("::std::uint32_t", "kLexLiveAt", live.at);
  if (reserving) {
    const TermRows res(std::span(a.dfaResSet.data(), a.ndfa), words);
    o.array("::std::uint64_t", "kLexRes", res.pool);
    o.array("::std::uint32_t", "kLexResAt", res.at);
  }
  if (relayReserving) {
    const TermRows held(std::span(a.dfaRelayResSet.data(), a.ndfa), words);
    o.array("::std::uint64_t", "kLexRelayRes", held.pool);
    o.array("::std::uint32_t", "kLexRelayResAt", held.at);
  }
  o.array("::cursiv::LlAtnState", "kAtnStates",
          std::span(a.states.data(), a.nstates), [](const LlAtnState& x) {
            return std::format("{{::cursiv::LlAtnKind({}),{}u,{}u,{}u,{}u}}",
                               static_cast<unsigned>(x.kind), x.rule,
                               x.decision, x.edge, x.nedges);
          });
  o.array("::cursiv::LlAtnEdge", "kAtnEdges",
          std::span(a.edges.data(), a.nedges), [](const LlAtnEdge& x) {
            return std::format("{{::cursiv::LlEdgeKind({}),{}u,{}u,{}u}}",
                               static_cast<unsigned>(x.kind), x.label, x.target,
                               x.follow);
          });
  o.array("::std::uint32_t", "kRuleStart", a.ruleStart);
  o.array("::std::uint32_t", "kCallerBase", a.callerBase);
  o.array("::std::uint32_t", "kCallerAt", a.callerAt);
  o.array("::std::uint32_t", "kReturnClass", a.returnClass);
  if (callers) {
    // Only the states a call returns into: no frame names any other.
    const LlExitReads exits(a);
    std::vector<DynSet> rows(a.nstates);
    for (const std::uint32_t r : a.callerAt) rows[r] = exits.reads[r];
    const TermRows pool(rows, words);
    std::vector<std::uint32_t> at(a.nstates);
    for (std::uint32_t s = 0; s < a.nstates; ++s) at[s] = pool.at[s] << 1;
    for (const std::uint32_t r : a.callerAt) at[r] |= exits.ends[r];
    o.array("::std::uint64_t", "kCallerReads", pool.pool);
    o.array("::std::uint32_t", "kCallerReadAt", at);
  }
  if (flagging) {
    const TermRows flags(a.decFlag, words);
    o.array("::std::uint64_t", "kDecFlag", flags.pool);
    o.array("::std::uint32_t", "kDecFlagAt", flags.at);
  }
  o.array("::std::uint8_t", "kSharedRules", a.sharedRules);
  o.array("::std::uint8_t", "kRepeatScopes", a.repeatScopes);
  std::vector<std::uint64_t> first(std::size_t{g.ndecisions} * words, 0);
  for (std::uint16_t d = 0; d < g.ndecisions; ++d) {
    const LlDecision& dec = g.decisions[d];
    for (std::uint16_t i = 0; i < g.decisionBuild[d].nalts; ++i)
      for (std::uint32_t k = 0; k < words; ++k)
        first[std::size_t{d} * words + k] |= a.laSet[dec.set + i].word(k);
  }
  o.array("::std::uint64_t", "kDecFirst", first);
  o.array("::std::uint32_t", "kNodeAfter", a.nodeAfter);
  std::vector<std::uint16_t> afterGap = a.afterGap;
  for (std::size_t id = 0; id < afterGap.size(); ++id)
    if (opMode[id] != kNoRow) afterGap[id] = kNoRow;
  o.array("::std::uint16_t", "kNodeAfterGap", afterGap);
  // Per position, the operator step it reads in a climb with a step that
  // groups nothing (`LlTable::nodeStep`). An operator of several tokens names
  // none.
  std::vector<std::uint32_t> steps;
  for (std::uint16_t k = 0; k < g.nclimbs; ++k) {
    const auto ops = std::span(g.climbOps.data() + g.climbs[k].opBase,
                               g.climbs[k].opCount);
    if (std::ranges::none_of(ops, [](const LlClimbOp& op) {
          return op.assoc == Association::non;
        }))
      continue;
    steps.resize(a.nodeAfter.size());
    for (const LlClimbOp& op : ops)
      if (const std::uint16_t n = g.atomic_operator(op.body); n != kNoRow)
        steps[n] = (std::uint32_t(k + 1u) << 16) |
                   (std::uint32_t{op.prec} << 1) |
                   (op.assoc == Association::non ? 1u : 0u);
  }
  o.array("::std::uint32_t", "kNodeStep", steps);
  o.array("::std::uint16_t", "kEdgeNode", a.edgeNode);
  o.array("::std::uint16_t", "kReturnNode", a.returnNode);
  if (fuseTable)
    o.array("::cursiv::LlFuse", "kFuse", a.fuse, [](const LlFuse& x) {
      return std::format("{{{}u,{}u}}", x.from, x.to);
    });

  if (!a.lookMode.empty()) o.array("::std::uint16_t", "kLookMode", a.lookMode);
  if (!a.sectionErr.empty()) {
    o.array("::std::uint16_t", "kSectionErr", a.sectionErr);
    o.array("::std::uint16_t", "kSectionEnd", a.sectionEnd);
  }
  o.array("::std::uint16_t", "kWordBounds", a.wordBounds);
  o.array("::cursiv::unicode::Range", "kWordRanges", g.wordRanges.v, range);
  if (classes) {
    std::vector<std::uint32_t> scalars(g.nsets);
    for (std::uint16_t i = 0; i < g.nsets; ++i)
      scalars[i] = lex_set_scalars(g, i);
    o.array("::std::uint32_t", "kSetScalars", scalars);
    o.array("::cursiv::unicode::Range", "kScalarRanges", g.scalarRanges.v,
            range);
  }
  // The functions the view points at, as the header declares them: their
  // definitions, and the large bodies only they call, go to the parser's own
  // unit. A unit that reads the view compiles every function it points at;
  // an inline function no one in the unit calls costs it nothing but its
  // text, so the predicates the conditions read stay in the header.
  const std::string_view conditionFn =
      "bool condition(const char* src, ::cursiv::TextOffset len, unsigned k,\n"
      "    const ::cursiv::Token& token, ::cursiv::TextOffset gapFrom, "
      "bool atEnd,\n    ::cursiv::TextOffset* seen)";
  const std::string_view namedFn =
      "unsigned condition_named(const char* src, ::cursiv::TextOffset len,\n"
      "    unsigned k, const ::cursiv::Token& token, "
      "::cursiv::TextOffset gapFrom)";
  const std::string_view headRefusesFn =
      "bool head_refuses(::cursiv::SymbolId t, const char* p,\n"
      "    ::cursiv::TextOffset rest)";
  const std::string_view headReachFn =
      "::cursiv::TextOffset head_reach(unsigned m, const char* p,\n"
      "    ::cursiv::TextOffset rest)";
  const std::string_view termReachFn =
      "::cursiv::TextOffset term_reach(::cursiv::SymbolId t, const char* p,\n"
      "    ::cursiv::TextOffset rest)";
  const std::string_view tailRefusesFn =
      "bool tail_refuses(::cursiv::SymbolId t, const char* p,\n"
      "    ::cursiv::TextOffset rest)";
  o.line(predicates.declarations());
  // A head leaf a terminal's body decides answers a caret with no bytes.
  std::vector<bool> named(g.ngapLeaves);
  const NamedHead reads(g);
  for (std::size_t l = 0; l < g.gapLeaves.size(); ++l) {
    named[l] = g.gapLeaves[l].kind == GapLeafKind::head &&
               reads.any(g.gapLeaves[l].node);
    if (named[l])
      fn.line(reads.source("inline", std::format("named_{}", l),
                           g.gapLeaves[l].node));
  }
  if (g.ngapConds != 0) {
    o.fmt("{};\n{};\n", conditionFn, namedFn);
    if (gaps.reads_boundary())
      o.fmt(
          "inline constexpr ::cursiv::LexBoundary kGapBoundary{{{}, {}, {{}}, "
          "{}, {}u, {}, {}}};\n",
          o.ref("kSets"), o.ref("kSetDecode"), o.ref("kWordRanges"),
          g.wordRanges.size(), classes ? "kSetScalars" : "nullptr",
          classes ? "kScalarRanges" : "nullptr");
    o.line(gaps.source());
    const auto leaf = [&](unsigned l, bool partial) -> std::string {
      switch (g.gapLeaves[l].kind) {
        case GapLeafKind::has:
        case GapLeafKind::is: {
          const auto expression = gaps.expression(l);
          return partial ? "(2u * " + expression + ")" : expression;
        }
        case GapLeafKind::end:
          return partial ? "(2u * (token.sym == ::cursiv::kEofSymbol))"
                         : "(atEnd || ::cursiv::gap_end(len, token))";
        case GapLeafKind::cut: {
          const auto in = gap_cut_expression(g, g.gapLeaves[l], "token.sym");
          return partial ? "(2u * " + in + ")" : in;
        }
        case GapLeafKind::head:
        case GapLeafKind::phrase:
          if (partial)
            return named[l] ? std::format("named_{}(token.sym)", l) : "1u";
          return std::format("predicate_{}(src, len, token, gapFrom, seen)", l);
      }
      return "false";
    };
    for (const bool partial : {false, true}) {
      fn.fmt("{} {{\n", partial ? namedFn : conditionFn);
      fn.line(
          "  const auto end = ::std::min(token.begin, len);\n"
          "  const auto begin = ::std::min(gapFrom, end);\n"
          "  const char* const g = src + begin;\n"
          "  const auto n = end - begin;\n  (void)g; (void)n;");
      if (!partial) fn.line("  (void)atEnd; (void)seen;");
      fn.line("  switch (k) {");
      for (unsigned k = 0; k < g.ngapConds; ++k)
        fn.fmt("    case {}u: return {};\n", k,
               GapConditionEmitter(g).source(
                   k, [&](unsigned l) { return leaf(l, partial); }, partial));
      fn.line("    default: return 0;\n  }\n}");
    }
  }
  const bool refusing = std::ranges::any_of(
      a.termHeads, [](const HeadMatcher& h) { return !h.words.empty(); });
  if (refusing) {
    o.fmt("{};\n{};\n{};\n", headRefusesFn, headReachFn, termReachFn);
    fn.line(lex_head_source(
        a.termHeads, a.modeHeads, headRefusesFn, headReachFn,
        {std::format("{}, {}u", o.ref("kWordRanges"), g.wordRanges.size()),
         o.ref("kScalarRanges")}));
    fn.fmt("{} {{\n", termReachFn);
    fn.line("  ::cursiv::TextOffset seen = 0;\n  switch (t) {");
    for (std::size_t t = 0; t < a.termHeads.size(); ++t)
      if (!a.termHeads[t].words.empty())
        fn.fmt(
            "    case {0}u:\n      reaches_{0}(p, rest, rest, seen);\n"
            "      break;\n",
            t);
    fn.line("    default:\n      break;\n  }\n  return seen;\n}");
  }
  const bool trailing = std::ranges::any_of(
      a.termTails, [](const HeadMatcher& h) { return !h.words.empty(); });
  if (trailing) {
    o.fmt("{};\n", tailRefusesFn);
    fn.line(lex_tail_source(
        a.termTails, tailRefusesFn,
        {std::format("{}, {}u", o.ref("kWordRanges"), g.wordRanges.size()),
         o.ref("kScalarRanges")}));
  }
  o.line("");
  o.line("// The generated walk of every mode, in the parser's own unit.");
  o.line("::cursiv::Scan walk_all(const char* src, ::cursiv::TextOffset len,");
  o.line(
      "                        ::std::uint16_t mode, ::cursiv::TextOffset i);");
  if (!a.relays.empty())
    o.line(
        "::cursiv::TextOffset relay_run(::std::uint16_t state, const char* "
        "src,\n    ::cursiv::TextOffset len, ::cursiv::TextOffset i);");
  o.line("");
  o.line("inline constexpr ::cursiv::LlTable kTable{");
  o.ptr("names", "kNames");
  o.ptr("terms", "kTerms");
  o.fmt("    .nterms = {}u,\n", g.nterms);
  o.fmt("    .nrules = {}u,\n", g.nrules);
  o.ptr("nodes", "kNodes");
  o.ptr("decisions", "kDecisions");
  o.fmt("    .ndecisions = {}u,\n", g.ndecisions);
  if (g.ngapConds != 0) {
    o.line("    .condition = &condition, .conditionNamed = &condition_named,");
    o.fmt("    .ngapConds = {}u,\n", g.ngapConds);
    const CondReach reach = GapReach(g).widest();
    o.fmt("    .condReach = {{{}u, {}u, {}, {}u}},\n", reach.look, reach.tail,
          reach.phrase ? "true" : "false", reach.tokens);
  }
  o.ptr("decFirst", "kDecFirst");
  o.ptr("nodeAfter", "kNodeAfter");
  o.ptr("nodeAfterGap", "kNodeAfterGap");
  o.ptr("nodeStep", "kNodeStep");
  o.fmt("    .nafter = {}u,\n", (unsigned)a.nodeAfter.size());
  o.ptr("edgeNode", "kEdgeNode");
  o.ptr("returnNode", "kReturnNode");
  o.fmt("    .atnStart = {}u,\n", a.atnEntry);
  o.ptr("sets", "kSets");
  o.ptr("setDecode", "kSetDecode");
  o.fmt("    .wordBytes = {},\n", byte_set(g.wordBytes));
  o.ptr("wordRanges", "kWordRanges");
  o.fmt("    .nwordRanges = {}u,\n", g.wordRanges.size());
  if (classes) {
    o.ptr("setScalars", "kSetScalars");
    o.ptr("scalarRanges", "kScalarRanges");
  }
  if (refusing)
    o.line(
        "    .head_refuses = &head_refuses,\n    .head_reach = &head_reach,\n"
        "    .termReach = &term_reach,");
  if (trailing) o.line("    .tailRefuses = &tail_refuses,");
  o.ptr("cls", "kCls");
  o.fmt("    .ncls = {}u,\n", a.ncls);
  o.fmt("    .nmodes = {}u,\n    .errMode = {}u,\n    .tailMode = {}u,\n",
        a.nmodes, a.errMode, a.tailMode);
  if (!a.lookMode.empty()) o.ptr("lookMode", "kLookMode");
  o.ptr("modeStart", "kModeStart");
  o.ptr("modeMulti", "kModeMulti");
  o.ptr("modeBacktrack", "kModeBacktrack");
  if (longer) o.ptr("modeLonger", "kModeLonger");
  o.ptr("modeTrivia", "kModeTrivia");
  o.ptr("modeJoinedTrivia", joinedApart ? "kModeJoinedTrivia" : "kModeTrivia");
  o.ptr("modeTerms", "kModeTerms");
  o.ptr("modeSub", "kModeSub");
  o.fmt("    .subWords = {}u,\n", a.subWords);
  o.ptr("skipSet", "kSkipSet");
  o.ptr("byteTerms", "kByteTerms");
  o.ptr("modeBytes", "kModeBytes");
  o.line("    .walk = &walk_all,");
  o.ptr("lexNext", "kLexNext");
  o.ptr("typoSuccBase", "kTypoSuccBase");
  o.ptr("typoSucc", "kTypoSucc");
  o.ptr("lexBodies", "kLexBodies");
  o.ptr("lexOwed", "kLexOwed");
  o.ptr("lexOpeners", "kLexOpeners");
  o.ptr("dfaAccept", "kLexAccept");
  o.ptr("dfaBound", "kLexBound");
  o.ptr("lexChoiceAt", "kLexChoiceAt");
  o.ptr("lexChoices", "kLexChoices");
  if (lexChecks) o.ptr("lexChecks", "kLexChecks");
  if (!a.relays.empty()) {
    o.ptr("dfaRelay", "kLexRelay");
    o.ptr("relays", "kRelays");
    o.fmt("    .nrelays = {}u,\n", a.relays.size());
    o.line("    .relay_run = &relay_run,");
  }
  o.ptr("lexAcc", "kLexAcc");
  o.ptr("lexAccAt", "kLexAccAt");
  if (reserving) {
    o.ptr("lexRes", "kLexRes");
    o.ptr("lexResAt", "kLexResAt");
  }
  o.ptr("lexLive", "kLexLive");
  o.ptr("lexLiveAt", "kLexLiveAt");
  if (relayReserving) {
    o.ptr("lexRelayRes", "kLexRelayRes");
    o.ptr("lexRelayResAt", "kLexRelayResAt");
  }
  o.ptr("wordBounds", "kWordBounds");
  if (reserving) o.line("    .reserves = true,");
  o.fmt("    .termWords = {}u,\n", words);
  o.fmt("    .atn = {{.states = {},\n", o.ref("kAtnStates"));
  o.fmt("            .nstates = {}u,\n", a.nstates);
  o.fmt("            .edges = {},\n", o.ref("kAtnEdges"));
  o.fmt("            .ruleStart = {},\n", o.ref("kRuleStart"));
  o.fmt("            .callerBase = {},\n", o.ref("kCallerBase"));
  o.fmt("            .callerAt = {},\n", o.ref("kCallerAt"));
  o.fmt("            .returnClass = {}}},\n", o.ref("kReturnClass"));
  if (callers) {
    o.ptr("callerReads", "kCallerReads");
    o.ptr("callerReadAt", "kCallerReadAt");
  }
  if (flagging) {
    o.ptr("decFlag", "kDecFlag");
    o.ptr("decFlagAt", "kDecFlagAt");
  }
  if (staged) o.line("    .staged = &ll_staged_predict,");
  o.ptr("sharedRules", "kSharedRules");
  o.ptr("repeatScopes", "kRepeatScopes");
  if (!a.sectionErr.empty()) {
    o.ptr("sectionErr", "kSectionErr");
    o.ptr("sectionEnd", "kSectionEnd");
    o.fmt("    .nsections = {}u,\n", a.sectionErr.size());
  }
  if (a.stamp != StampLayout{})
    o.fmt("    .stamp = {{.rowBits = {}u}},\n", a.stamp.rowBits);
  o.line("};");
  o.line("");
  if (compiledPredicates)
    o.line(predicates.source("::cursiv::LlLexTape<kTable>{}"));
  o.line("");
  if (staged) fn.out += staged_source(a);
  fn.out += lexical.source();
  o.fmt("}}  // namespace {}\n", ns);
  return {std::move(o.out), std::move(includes), std::move(fn.out)};
}

}  // namespace cursiv
