// The table as source: every array the runtime reads, and the one view that
// names them. Nothing here is computed twice -- what the construction settled
// is printed, and what a reader derives is printed too, so no module folds a
// question out of the table at load time.
#include "cursiv/engines/ll/gen/ll_emit_table.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engines/ll/gen/LlStampPlan.hpp"
#include "cursiv/engines/shared/gen/CLiteral.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/GapConditionEmitter.hpp"
#include "cursiv/engines/shared/gen/GapEmitter.hpp"
#include "cursiv/engines/shared/gen/GapReach.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LexicalPredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/NamedHead.hpp"
#include "cursiv/engines/shared/gen/PredicateEmitter.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"

namespace cursiv {
namespace {

using Built = LlBuiltProgram;

std::string u(std::uint64_t v) { return std::format("{}u", v); }

struct Out : EmitOutput {
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

bool reserves(const Built& a) {
  return a.dfaResSet.size() >= a.ndfa &&
         std::any_of(a.dfaResSet.data(), a.dfaResSet.data() + a.ndfa,
                     [](const DynSet& set) { return set.any(); });
}

bool longer_modes(const Built& a) {
  return std::any_of(a.modeLonger.begin(), a.modeLonger.end(),
                     [](std::uint16_t m) { return m != kNoMode; });
}

bool relay_reserves(const Built& a) {
  return a.dfaRelayResSet.size() >= a.ndfa &&
         std::any_of(a.dfaRelayResSet.data(), a.dfaRelayResSet.data() + a.ndfa,
                     [](const DynSet& set) { return set.any(); });
}

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
  o.line(
      "inline ::std::uint16_t ll_staged_predict(const ::cursiv::LlContext& c,");
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

std::string ll_emit_table(const Built& a, const LlStampPlan& plan,
                          const std::string& ns) {
  Out o;
  const auto& g = a.cfg;
  const GapEmitter gaps(g, "kSets", "kGapBoundary", "inline");
  const PredicateEmitter predicates(g, gaps, a.sectionErr);
  const LexicalPredicateEmitter lexical(g, a.relays,
                                        "::cursiv::lex_boundary(kTable)", true);
  const bool compiledPredicates = std::ranges::any_of(
      g.gapLeaves.v, [](const GapLeaf& leaf) { return leaf.reads_tokens(); });
  const bool lexChecks = std::ranges::any_of(
      a.lexChecks.v, [](std::uint8_t checks) { return checks != 0; });
  const bool staged = std::ranges::any_of(
      g.decisions.v,
      [](const LlDecision& d) { return d.how == LlPredict::staged; });
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
  if (fuseTable) o.line("#include \"cursiv/engines/ll/table/LlFuse.hpp\"");
  o.line("#include \"cursiv/core/utils/Utf8.hpp\"");
  o.line("#include \"cursiv/core/utils/macros.hpp\"");
  o.line("#include \"cursiv/engines/shared/runtime/lex_scalar_in.hpp\"");
  o.line("");
  if (compiledPredicates) {
    o.line("#include \"cursiv/engines/ll/runtime/ll_matcher.hpp\"");
    o.line("#include \"cursiv/engines/shared/runtime/LexSuffixSession.hpp\"");
  }
  if (staged) o.line("#include \"cursiv/engines/ll/runtime/ll_predictor.hpp\"");
  if (compiledPredicates || lexical.any() || g.ngapConds != 0)
    o.line("#include \"cursiv/engines/shared/runtime/LexBoundary.hpp\"");
  if (compiledPredicates || lexical.any())
    o.line("#include \"cursiv/engines/shared/runtime/PredicateState.hpp\"");
  if (lexical.any()) {
    o.line("#include <exception>");
    o.line("#include \"cursiv/engines/shared/runtime/LexicalMachine.hpp\"");
  }
  if (lexical.searches())
    o.line("#include \"cursiv/engines/shared/runtime/lex_find.hpp\"");
  if (g.ngapConds != 0 || lexical.any())
    o.line("#include \"cursiv/engines/shared/runtime/GapPositions.hpp\"");
  o.fmt("namespace {} {{\n", ns);
  o.line("");
  if (staged)
    o.line(
        "inline ::std::uint16_t ll_staged_predict(const ::cursiv::LlContext&, "
        "::std::uint16_t);");
  o.line(lexical.declarations());
  if (!a.relays.empty()) {
    o.line(
        "inline ::cursiv::TextOffset lexical_run(::std::uint16_t state, "
        "[[maybe_unused]] const char* src,");
    o.line(
        "    [[maybe_unused]] ::cursiv::TextOffset len, "
        "::cursiv::TextOffset at) {");
    o.line("  switch (state) {");
    // The cases are the states a relay runs through blind, and no other.
    for (unsigned st = 0; st < a.ndfa; ++st) {
      if (!lex_run_blind(a.dfaAccept[st] != 0,
                         a.dfaBound[st] != kNoSet || a.lexChecks[st] != 0,
                         a.dfaRelay[st] != kNoRelay))
        continue;
      ByteSet stay;
      for (unsigned x = 0; x < 256; ++x)
        if (a.dfaNext[std::size_t{st} * a.ncls + a.cls[x]] == st)
          stay.add(static_cast<unsigned char>(x));
      if (!stay.any()) continue;
      o.fmt("    case {}u: {{\n", st);
      std::string test;
      unsigned ranges = 0;
      for (unsigned lo = 0; lo < 256; ++lo) {
        if (!stay.has(static_cast<unsigned char>(lo))) continue;
        unsigned hi = lo;
        while (hi < 255 && stay.has(static_cast<unsigned char>(hi + 1))) ++hi;
        if (!test.empty()) test += " || ";
        test += std::format(
            "(static_cast<unsigned>(static_cast<unsigned char>(src[at])) - {}u "
            "<= {}u)",
            lo, hi - lo);
        ++ranges;
        lo = hi;
      }
      if (ranges > 4) {
        o.fmt(
            "      constexpr ::cursiv::ByteSet "
            "bytes{{{{{}ull,{}ull,{}ull,{}ull}}}};\n",
            stay.w[0], stay.w[1], stay.w[2], stay.w[3]);
        test = "bytes.has(static_cast<unsigned char>(src[at]))";
      }
      o.fmt("      while (at < len && ({})) ++at;\n", test);
      o.line("      return at;\n    }");
    }
    o.line("    default: return at;\n  }\n}");
  }

  o.line("inline constexpr char kNames[] =");
  const std::string_view names(g.names.data(), g.nameLen);
  for (std::size_t at = 0; at < names.size(); at += 60)
    o.fmt("    {}\n", c_literal(names.substr(at, 60)));
  o.line(";");

  o.array("::cursiv::ByteSet", "kSets", std::span(g.sets.data(), g.nsets),
          [](const ByteSet& s) {
            return std::format("{{{{{}u,{}u,{}u,{}u}}}}", s.w[0], s.w[1],
                               s.w[2], s.w[3]);
          });
  o.array("::std::uint16_t", "kSetDecode", g.setDecode.v);
  o.array("::cursiv::TermInfo", "kTerms", std::span(g.terms.data(), g.nterms),
          [](const TermInfo& x) {
            return std::format(
                "{{{}u,{}u,{}u,{},{},{},{}u,{}u,{},{},{}u,{}u,{}}}", x.nameOff,
                x.nameLen, x.element, x.trivia, x.recorded, x.spelled,
                x.textOff, x.textLen, x.delim, x.joins, x.laid, x.cond,
                x.refuses);
          });
  o.array("::cursiv::LlTable::Rule", "kRules",
          std::span(g.rules.data(), g.nrules), [](const LlRuleInfo& x) {
            return std::format("{{{}u,{}u,{}u}}", x.nameOff, x.nameLen,
                               x.climb);
          });
  o.array("::cursiv::LlTable::Node", "kNodes",
          std::span(g.nodes.data(), g.nnodes), [&](const LlNode& x) {
            const auto id = static_cast<std::size_t>(&x - g.nodes.data());
            const auto mode =
                x.kind == LlShape::term && a.nodeAfter[id] != 0xFFFFFFFFu
                    ? a.nodeAfterMode[id]
                    : x.mode;
            return std::format(
                "{{{}u,{}u,{}u,{},{}u}}", x.row, mode,
                x.kind == LlShape::term || x.kind == LlShape::implied ? x.sym
                                                                      : kNoRow,
                a.nodeHead[id] != 0,
                plan.covers(static_cast<std::uint16_t>(id)));
          });
  o.array("::cursiv::LlTable::Decision", "kDecisions",
          std::span(g.decisions.data(), g.ndecisions),
          [&](const LlDecision& x) {
            return std::format(
                "{{{},::cursiv::LlPredict({}),{}u,{}u,{}u}}",
                x.node != kNoRow && g.nodes[x.node].kind == LlShape::rep,
                static_cast<unsigned>(x.how), x.mode, x.state, x.rule);
          });
  o.array("::cursiv::LlTable::Climb", "kClimbs",
          std::span(g.climbs.data(), g.nclimbs), [&](const LlClimb& x) {
            return std::format("{{{}u,{}u,{}u}}", x.atnEntry, x.opCount,
                               x.selectNode == kNoRow
                                   ? kNoRow
                                   : g.nodes[x.selectNode].decision);
          });
  o.array("::std::uint8_t", "kCls", std::span(a.cls));
  {
    o.array("::std::uint16_t", "kModeStart",
            std::span(a.modeStart.data(), a.nmodes));
    o.array("::std::uint8_t", "kModeMulti",
            std::span(a.modeMulti.data(), a.nmodes));
    if (longer_modes(a))
      o.array("::std::uint16_t", "kModeLonger", a.modeLonger);
    o.array("::std::uint16_t", "kModeTrivia", a.modeTrivia);
    o.array("::std::uint16_t", "kModeJoinedTrivia", a.modeJoinedTrivia);
    o.array("::std::uint64_t", "kModeTerms", a.modeTerms);
    o.array("::std::uint64_t", "kModeSub", a.modeSub);
  }
  {
    o.array("::std::uint16_t", "kLexNext",
            std::span(a.dfaNext.data(), std::size_t{a.ndfa} * a.ncls));
    o.array("::std::uint32_t", "kTypoSuccBase", a.typoSuccBase);
    o.array("::std::uint16_t", "kTypoSucc", a.typoSucc);
    o.array("::std::uint8_t", "kLexBodies", a.lexBodies);
    o.array("::std::uint16_t", "kLexOwed", a.lexOwed);
    o.array("::cursiv::ByteSet", "kLexOpeners", a.lexOpeners,
            [](const ByteSet& x) {
              return std::format("{{{{{}u,{}u,{}u,{}u}}}}", x.w[0], x.w[1],
                                 x.w[2], x.w[3]);
            });
    o.array("::std::uint16_t", "kLexAccept",
            std::span(a.dfaAccept.data(), a.ndfa));
    o.array("::std::uint16_t", "kLexBound",
            std::span(a.dfaBound.data(), a.ndfa));
    o.array("::std::uint32_t", "kLexChoiceAt", a.lexChoiceAt.v);
    o.array("::cursiv::LlLexAccept", "kLexChoices", a.lexChoices,
            [](const LlLexAccept& x) {
              return std::format("{{{}u,{}u,{}u}}", x.term, x.bound, x.tier);
            });
    if (lexChecks) o.array("::std::uint8_t", "kLexChecks", a.lexChecks.v);
    if (!a.relays.empty()) {
      o.array("::std::uint16_t", "kLexRelay",
              std::span(a.dfaRelay.data(), a.ndfa));
      o.array(
          "::cursiv::LexRelay", "kRelays", a.relays, [](const LexRelayGen& x) {
            return lex_relay_source(x, [](std::uint16_t off) { return off; });
          });
    }
    const std::uint32_t words = (g.nterms + 63u) / 64u;
    const TermRows acc(std::span(a.dfaAccSet.data(), a.ndfa), words);
    const TermRows res(std::span(a.dfaResSet.data(), a.ndfa), words);
    o.array("::std::uint64_t", "kLexAcc", acc.pool);
    o.array("::std::uint32_t", "kLexAccAt", acc.at);
    o.array("::std::uint64_t", "kLexRes", res.pool);
    o.array("::std::uint32_t", "kLexResAt", res.at);
    if (relay_reserves(a)) {
      const TermRows held(std::span(a.dfaRelayResSet.data(), a.ndfa), words);
      o.array("::std::uint64_t", "kLexRelayRes", held.pool);
      o.array("::std::uint32_t", "kLexRelayResAt", held.at);
    }
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
  {
    const std::uint32_t words = (g.nterms + 63u) / 64u;
    std::vector<std::uint64_t> first(std::size_t{g.ndecisions} * words, 0);
    for (std::uint16_t d = 0; d < g.ndecisions; ++d) {
      const LlDecision& dec = g.decisions[d];
      for (std::uint16_t i = 0; i < g.decisionBuild[d].nalts; ++i)
        for (std::uint32_t k = 0; k < words; ++k)
          first[std::size_t{d} * words + k] |= a.laSet[dec.set + i].word(k);
    }
    o.array("::std::uint64_t", "kDecFirst", first);
    std::vector<std::uint64_t> follow(std::size_t{g.nrules} * words, 0);
    for (std::uint16_t r = 0; r < g.nrules; ++r)
      for (std::uint32_t k = 0; k < words; ++k)
        follow[std::size_t{r} * words + k] = a.ruleFollow[r].word(k);
    o.array("::std::uint64_t", "kRuleFollow", follow);
  }
  o.array("::std::uint32_t", "kNodeAfter", a.nodeAfter);
  o.array("::std::uint16_t", "kNodeAfterMode", a.nodeAfterMode);
  if (!a.afterGap.empty())
    o.array("::std::uint16_t", "kNodeAfterGap", a.afterGap);
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
  o.array("::cursiv::unicode::Range", "kWordRanges", g.wordRanges.v,
          [](const unicode::Range& r) {
            return std::format("{{{}u,{}u}}", static_cast<std::uint32_t>(r.lo),
                               static_cast<std::uint32_t>(r.hi));
          });
  const bool classes = g.scalarRanges.size() != 0;
  if (classes) {
    std::vector<std::uint32_t> scalars(g.nsets);
    for (std::uint16_t i = 0; i < g.nsets; ++i)
      scalars[i] = lex_set_scalars(g, i);
    o.array("::std::uint32_t", "kSetScalars", scalars);
    o.array("::cursiv::unicode::Range", "kScalarRanges", g.scalarRanges.v,
            [](const unicode::Range& r) {
              return std::format("{{{}u,{}u}}",
                                 static_cast<std::uint32_t>(r.lo),
                                 static_cast<std::uint32_t>(r.hi));
            });
  }
  o.line(predicates.declarations());
  // A head leaf a terminal's body decides answers a caret with no bytes.
  std::vector<bool> named(g.ngapLeaves);
  const NamedHead reads(g);
  for (std::size_t l = 0; l < g.gapLeaves.size(); ++l) {
    named[l] = g.gapLeaves[l].kind == GapLeafKind::head &&
               reads.any(g.gapLeaves[l].node);
    if (named[l])
      o.line(reads.source("inline", std::format("named_{}", l),
                          g.gapLeaves[l].node));
  }
  if (g.ngapConds != 0) {
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
      o.line(partial ? "inline unsigned condition_named(const char* src, "
                       "::cursiv::TextOffset len, unsigned k,"
                     : "inline bool condition(const char* src, "
                       "::cursiv::TextOffset len, unsigned k,");
      o.line(partial ? "    const ::cursiv::Token& token, ::cursiv::TextOffset "
                       "gapFrom) {"
                     : "    const ::cursiv::Token& token, ::cursiv::TextOffset "
                       "gapFrom, bool atEnd,"
                       "    ::cursiv::TextOffset* seen) {");
      o.line(
          "  const auto end = ::std::min(token.begin, len);\n"
          "  const auto begin = ::std::min(gapFrom, end);\n"
          "  const char* const g = src + begin;\n"
          "  const auto n = end - begin;\n  (void)g; (void)n;");
      if (!partial) o.line("  (void)atEnd; (void)seen;");
      o.line("  switch (k) {");
      for (unsigned k = 0; k < g.ngapConds; ++k)
        o.fmt("    case {}u: return {};\n", k,
              GapConditionEmitter(g).source(
                  k, [&](unsigned l) { return leaf(l, partial); }, partial));
      o.line("    default: return 0;\n  }\n}");
    }
  }
  const bool refusing = std::ranges::any_of(
      a.termHeads, [](const HeadMatcher& h) { return !h.words.empty(); });
  if (refusing)
    o.line(lex_head_source(
        a.termHeads, a.modeHeads,
        "inline bool head_refuses(::cursiv::SymbolId t, const char* p,\n"
        "    ::cursiv::TextOffset rest)",
        "inline ::cursiv::TextOffset head_reach(unsigned m, const char* p,\n"
        "    ::cursiv::TextOffset rest)",
        {std::format("{}, {}u", o.ref("kWordRanges"), g.wordRanges.size()),
         o.ref("kScalarRanges")},
        HeadMatcher::Reach::prefixes));
  const bool behind = std::ranges::any_of(
      a.termHeads,
      [](const HeadMatcher& h) { return !h.words.empty() && h.offset != 0; });
  if (behind) {
    o.line("inline ::cursiv::TextOffset term_reach(::cursiv::SymbolId t,");
    o.line("    const char* p, ::cursiv::TextOffset rest) {");
    o.line("  ::cursiv::TextOffset seen = 0;\n  switch (t) {");
    for (std::size_t t = 0; t < a.termHeads.size(); ++t)
      if (!a.termHeads[t].words.empty() && a.termHeads[t].offset != 0)
        o.fmt(
            "    case {0}u:\n      reaches_{0}(p, rest, rest, seen);\n"
            "      break;\n",
            t);
    o.line("    default:\n      break;\n  }\n  return seen;\n}");
  }
  const bool trailing = std::ranges::any_of(
      a.termTails, [](const HeadMatcher& h) { return !h.words.empty(); });
  if (trailing)
    o.line(lex_tail_source(
        a.termTails,
        "inline bool tail_refuses(::cursiv::SymbolId t, const char* p,\n"
        "    ::cursiv::TextOffset rest)",
        {std::format("{}, {}u", o.ref("kWordRanges"), g.wordRanges.size()),
         o.ref("kScalarRanges")}));
  o.line("");
  o.line("inline constexpr ::cursiv::LlTable kTable{");
  o.ptr("names", "kNames");
  o.ptr("terms", "kTerms");
  o.fmt("    .nterms = {}u,\n", g.nterms);
  o.ptr("rules", "kRules");
  o.fmt("    .nrules = {}u,\n", g.nrules);
  o.fmt("    .entryRule = {}u,\n", g.entryRule);
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
  o.ptr("ruleFollow", "kRuleFollow");
  o.ptr("nodeAfter", "kNodeAfter");
  o.ptr("nodeAfterMode", "kNodeAfterMode");
  if (!a.afterGap.empty()) o.ptr("nodeAfterGap", "kNodeAfterGap");
  o.fmt("    .nafter = {}u,\n", (unsigned)a.nodeAfter.size());
  o.ptr("edgeNode", "kEdgeNode");
  o.ptr("returnNode", "kReturnNode");
  o.fmt("    .atnStart = {}u,\n", a.atnEntry);
  o.ptr("climbs", "kClimbs");
  o.ptr("sets", "kSets");
  o.ptr("setDecode", "kSetDecode");
  o.fmt("    .wordBytes = {{{{{}u,{}u,{}u,{}u}}}},\n", g.wordBytes.w[0],
        g.wordBytes.w[1], g.wordBytes.w[2], g.wordBytes.w[3]);
  o.ptr("wordRanges", "kWordRanges");
  o.fmt("    .nwordRanges = {}u,\n", g.wordRanges.size());
  if (classes) {
    o.ptr("setScalars", "kSetScalars");
    o.ptr("scalarRanges", "kScalarRanges");
  }
  if (refusing)
    o.line("    .head_refuses = &head_refuses,\n    .head_reach = &head_reach,");
  if (behind) o.line("    .termReach = &term_reach,");
  if (trailing) o.line("    .tailRefuses = &tail_refuses,");
  o.ptr("cls", "kCls");
  o.fmt("    .ncls = {}u,\n", a.ncls);
  o.fmt("    .nmodes = {}u,\n    .errMode = {}u,\n    .tailMode = {}u,\n",
        a.nmodes, a.errMode, a.tailMode);
  if (!a.lookMode.empty()) o.ptr("lookMode", "kLookMode");
  o.ptr("modeStart", "kModeStart");
  o.ptr("modeMulti", "kModeMulti");
  if (longer_modes(a)) o.ptr("modeLonger", "kModeLonger");
  if (!a.modeTrivia.empty()) {
    o.ptr("modeTrivia", "kModeTrivia");
    o.ptr("modeJoinedTrivia", "kModeJoinedTrivia");
  }
  o.ptr("modeTerms", "kModeTerms");
  o.ptr("modeSub", "kModeSub");
  o.fmt("    .subWords = {}u,\n", a.subWords);
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
    o.line("    .relay_run = &lexical_run,");
  }
  o.ptr("lexAcc", "kLexAcc");
  o.ptr("lexAccAt", "kLexAccAt");
  o.ptr("lexRes", "kLexRes");
  o.ptr("lexResAt", "kLexResAt");
  if (relay_reserves(a)) {
    o.ptr("lexRelayRes", "kLexRelayRes");
    o.ptr("lexRelayResAt", "kLexRelayResAt");
  }
  o.ptr("wordBounds", "kWordBounds");
  if (reserves(a)) o.line("    .reserves = true,");
  o.fmt("    .termWords = {}u,\n", (g.nterms + 63u) / 64u);
  o.fmt("    .atn = {{.states = {},\n", o.ref("kAtnStates"));
  o.fmt("            .nstates = {}u,\n", a.nstates);
  o.fmt("            .edges = {},\n", o.ref("kAtnEdges"));
  o.fmt("            .ruleStart = {},\n", o.ref("kRuleStart"));
  o.fmt("            .callerBase = {},\n", o.ref("kCallerBase"));
  o.fmt("            .callerAt = {},\n", o.ref("kCallerAt"));
  o.fmt("            .returnClass = {}}},\n", o.ref("kReturnClass"));
  if (staged) o.line("    .staged = &ll_staged_predict,");
  if (!a.sectionErr.empty()) {
    o.ptr("sectionErr", "kSectionErr");
    o.ptr("sectionEnd", "kSectionEnd");
    o.fmt("    .nsections = {}u,\n", a.sectionErr.size());
  }
  if (a.stamp != StampLayout{})
    o.fmt("    .stamp = {{.rowBits = {}u}},\n", a.stamp.rowBits);
  o.line("};");
  o.line("");
  if (staged) o.line(staged_source(a));
  o.line(lexical.source());
  if (compiledPredicates)
    o.line(predicates.source(
        "  if (seen) {\n"
        "    ::cursiv::TextOffset halt = at;\n"
        "    const auto sc = ::cursiv::ll_match_text<kTable>(src, len, mode, "
        "at, halt);\n"
        "    *seen = ::std::max(*seen, halt < len ? halt + 1 : len);\n"
        "    return sc;\n"
        "  }\n"
        "  return ::cursiv::ll_match_text<kTable>(src, len, mode, at);\n",
        "::cursiv::LlLexTape<kTable>{}"));
  o.fmt("}}  // namespace {}\n", ns);
  return o.out;
}

}  // namespace cursiv
