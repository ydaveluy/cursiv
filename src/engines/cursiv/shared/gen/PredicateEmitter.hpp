// Predicates compile to recognition functions over the normal lexer's modes.
// No grammar graph is interpreted while parsing.
#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/GapConditionEmitter.hpp"
#include "cursiv/shared/gen/GapEmitter.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/PredicateModes.hpp"
#include "cursiv/shared/gen/UnboundedCondition.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv {

template <class Cfg>
class PredicateEmitter {
 public:
  // `sectionErr[h]` is the mode the text of section `h` is read in, whole.
  PredicateEmitter(const Cfg& cfg, const GapEmitter<Cfg>& gaps,
                   std::span<const std::uint16_t> sectionErr = {})
      : _g(cfg),
        _gaps(gaps),
        _err(sectionErr),
        _modes(cfg),
        _used(cfg.nlex),
        _called(cfg.nlex),
        _head(cfg.nlex),
        _turn(cfg.nlex) {
    for (const auto& leaf : cfg.gapLeaves.v)
      if (leaf.reads_tokens()) {
        visit(leaf.node);
        if (leaf.skip != kNoLexNode) visit(leaf.skip);
      }
    _modes.trace();
    for (const auto& leaf : cfg.gapLeaves.v)
      if (leaf.reads_tokens()) head_tokens(leaf.node, leaf.trigger);
    for (const auto at : _order) {
      const auto kind = _g.lex[at].kind;
      if (kind != LexKind::token && _modes.lexes(kind))
        _reads.emplace_back(_g.lexMode[at], _modes.want(at));
    }
    for (const auto& leaf : cfg.gapLeaves.v)
      if (leaf.reads_tokens()) keep_runs(leaf.node);
    for (const auto at : _order) {
      const auto kind = _g.lex[at].kind;
      if (kind == LexKind::positive || kind == LexKind::negative)
        keep_runs(_g.lex[at].a);
    }
    // Only the nodes something calls are written: a phrase answered by its
    // runs calls its parts itself.
    for (const auto& leaf : cfg.gapLeaves.v)
      if (leaf.reads_tokens()) {
        if (!_holds.contains(leaf.node)) _called[leaf.node] = true;
        if (leaf.skip != kNoLexNode) _called[leaf.skip] = true;
      }
    for (auto it = _order.rbegin(); it != _order.rend(); ++it) {
      if (!_called[*it]) continue;
      const LexNode e = _g.lex[*it];
      if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
        for (unsigned i = 0; i < e.b; ++i) _called[_g.lexKid[e.a + i]] = true;
      } else if (e.kind == LexKind::rep) {
        if (e.max != 0) _called[e.a] = true;
        if (e.max > 1 && e.b != kNoLexNode) _called[e.b] = true;
      } else if ((e.kind == LexKind::positive || e.kind == LexKind::negative) &&
                 !_holds.contains(e.a)) {
        _called[e.a] = true;
      }
    }
  }

  std::string declarations() const {
    std::string out;
    for (unsigned l = 0; l < _g.ngapLeaves; ++l)
      if (compiled(l))
        out += concat("inline bool predicate_", l,
                      "(const char*, ::cursiv::TextOffset,\n"
                      "    const ::cursiv::Token&, ::cursiv::TextOffset,\n"
                      "    ::cursiv::TextOffset* = nullptr);\n");
    return out;
  }

  bool compiled(unsigned l) const { return _g.gapLeaves[l].reads_tokens(); }

  // `tape` names the lexer tape the generated walk `walk_all` reads.
  std::string source(std::string_view tape) const {
    std::string out;
    bool lexed = false;
    bool reserved = false;
    bool sectioned = false;
    for (unsigned l = 0; l < _g.ngapLeaves; ++l)
      sectioned = sectioned || (compiled(l) && home(l) != 0);
    for (const auto at : _order) {
      const LexNode& e = _g.lex[at];
      lexed = lexed || e.kind == LexKind::token || e.kind == LexKind::alt ||
              (e.kind == LexKind::rep && e.min < e.max) ||
              (e.kind == LexKind::skip && e.b != 0);
      reserved = reserved || (e.kind == LexKind::token && !_head[at] &&
                              !_g.terms[e.a].spelled);
    }
    // No condition's mode lexes for length (`predicate_modes`): the walk's
    // token is the mode's.
    if (lexed || sectioned)
      out += concat(
          "inline ::cursiv::Scan predicate_lex(const char* src,\n"
          "    ::cursiv::TextOffset len, ::cursiv::TextOffset at,\n"
          "    unsigned mode, ::cursiv::TextOffset* seen) {\n"
          "  auto tape = ",
          tape,
          ";\n"
          "  return ::cursiv::LexSuffixSession<decltype(tape)>::match(\n"
          "      tape, src, len, mode, at, seen, [&] {\n"
          "        if (!seen) return walk_all(src, len, mode, at);\n"
          "        ::cursiv::TextOffset halt = at;\n"
          "        const auto sc = ::cursiv::lex_match_halt(\n"
          "            tape, src, len, mode, at, halt);\n"
          "        *seen = ::std::max(*seen, halt < len ? halt + 1 : len);\n"
          "        return sc;\n"
          "      });\n"
          "}\n");
    if (reserved) {
      // The refusals a reserving keyword asks of the token read as a walk of
      // the mode from it does, halt counted as `predicate_lex` counts it.
      bool refusing = false;
      for (unsigned t = 1; t < _g.nterms; ++t)
        refusing = refusing || _g.terms[t].refuses;
      out += concat(
          "inline bool predicate_reserved(const char* src, "
          "::cursiv::TextOffset len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset end,\n"
          "    unsigned mode, ::cursiv::SymbolId term, ::cursiv::TextOffset* "
          "seen) {\n"
          "  const auto tape = ",
          tape,
          ";\n"
          "  auto state = tape.start(mode);\n"
          "  for (auto i = at; i < end; ++i) {\n"
          "    state = tape.step(state, static_cast<unsigned char>(src[i]));\n"
          "    if (state == ::cursiv::kDeadState) return false;\n"
          "  }\n"
          "  if (!kTable.dfa_res_set(state).test(term)) return false;\n"
          "  auto far = end;\n"
          "  const bool reserved = ::cursiv::lex_reserves<kTable>(\n"
          "      state, src, end, len, far, [&](::cursiv::SymbolId t) {\n"
          "        return !tape.refuses(t, src + at, len - at);\n"
          "      });\n",
          refusing
              ? "  if (seen) {\n"
                "    const auto halt = at + tape.head_reach(mode, src + at, "
                "len - at);\n"
                "    *seen = ::std::max({*seen, far, halt < len ? halt + 1 : "
                "len});\n"
                "  }\n"
              : "  if (seen) *seen = ::std::max(*seen, far);\n",
          "  return reserved;\n"
          "}\n");
    }
    if (sectioned)
      out +=
          "inline ::cursiv::TextOffset predicate_close(const char* src,\n"
          "    ::cursiv::TextOffset len, ::cursiv::TextOffset at,\n"
          "    ::cursiv::TextOffset upto, unsigned mode,\n"
          "    ::cursiv::SymbolId close, ::cursiv::TextOffset* seen) {\n"
          "  for (auto p = at; p < upto;) {\n"
          "    const auto sc = predicate_lex(src, len, p, mode, seen);\n"
          "    if (sc.term == ::cursiv::kInvalidSymbol || sc.end <= p) {\n"
          "      ++p;\n"
          "      continue;\n"
          "    }\n"
          "    if (sc.term == close) return p;\n"
          "    p = sc.end;\n"
          "  }\n"
          "  return len;\n"
          "}\n";
    if (_nruns != 0)
      out += concat(
          "inline ::cursiv::PredicateRuns* predicate_runs(const char* src,\n"
          "    ::cursiv::TextOffset len) {\n"
          "  return ::cursiv::LexSuffixSession<decltype(",
          tape,
          ")>::runs(src, "
          "len);\n"
          "}\n");
    out += _runDecls;
    for (const auto at : _order)
      if (_called[at]) out += node(at);
    out += _runDefs;
    std::map<std::string, std::string> knowns;
    for (unsigned l = 0; l < _g.ngapLeaves; ++l) {
      if (!compiled(l)) continue;
      const GapLeaf& leaf = _g.gapLeaves[l];
      // What the token in hand alone decides is answered before any reading.
      std::string known;
      ByteSet trivia;
      if (const auto answers = decided(leaf, trivia); !answers.empty()) {
        auto [at, fresh] =
            knowns.try_emplace(std::string(answers.begin(), answers.end()),
                               concat("kPredicateKnown_", knowns.size()));
        if (fresh) {
          out +=
              concat("inline constexpr ::std::uint8_t ", at->second, "[] = {");
          for (const std::uint8_t v : answers) out += concat(v, ",");
          out += "};\n";
        }
        known = std::format(
            "  if (la.end > la.begin && la.sym < {0}u && {1}[la.sym] != 0 &&\n"
            "      (la.begin >= len || !::cursiv::ByteSet{{{{0x{2:x}ull, 0x{3:x}ull, "
            "0x{4:x}ull, 0x{5:x}ull}}}}.has(static_cast<unsigned char>(src[la.begin]))))\n"
            "    return {1}[la.sym] == 2u;\n",
            answers.size(), at->second, trivia.w[0], trivia.w[1], trivia.w[2],
            trivia.w[3]);
      }
      const std::string start =
          "::cursiv::PredicateState{::cursiv::PredicateCursor{at, "
          "::std::min(from, at),\n"
          "      la.end > la.begin ? la.sym : ::cursiv::kUnread, la.mode, "
          "la.end}}";
      const unsigned h = home(l);
      out += concat(
          "inline bool predicate_", l, h != 0 ? "_in" : "",
          "(const char* src, ::cursiv::TextOffset "
          "len,\n"
          "    const ::cursiv::Token& la, ::cursiv::TextOffset from,\n"
          "    ::cursiv::TextOffset* seen) {\n",
          h != 0 ? "" : known,
          "  const auto at = ::std::min(la.begin, len);\n"
          "  return ",
          holds(leaf.node,
                leaf.skip == kNoLexNode ? start : call(leaf.skip, start)),
          ";\n"
          "}\n");
      // A section's predicate reads its text alone: one that looked as far as
      // the token closing the section is read again up to there.
      if (h != 0)
        out +=
            concat("inline bool predicate_", l,
                   "(const char* src, ::cursiv::TextOffset "
                   "len,\n"
                   "    const ::cursiv::Token& la, ::cursiv::TextOffset from,\n"
                   "    ::cursiv::TextOffset* seen) {\n"
                   "  ::cursiv::TextOffset reach = 0;\n"
                   "  const bool value = predicate_",
                   l,
                   "_in(src, len, la, from, "
                   "&reach);\n"
                   "  const auto close = predicate_close(src, len, "
                   "::std::min(la.begin, len),\n"
                   "      reach, ",
                   _err[h], "u, ", _g.cfgEnd[h],
                   "u, &reach);\n"
                   "  if (seen) *seen = ::std::max(*seen, reach);\n"
                   "  return close < reach\n"
                   "      ? predicate_",
                   l,
                   "_in(src, close, la, from, seen) : value;\n"
                   "}\n");
    }
    return out;
  }

 private:
  const Cfg& _g;
  const GapEmitter<Cfg>& _gaps;
  std::span<const std::uint16_t> _err;
  PredicateModes<Cfg> _modes;
  std::vector<bool> _used, _called;
  // The tokens a condition opens with, which read the token in hand, and those
  // of a commit's exit the rule has a terminal for.
  std::vector<bool> _head, _turn;
  std::vector<std::uint16_t> _order;
  // What `turns_of` answers, by terminal a turn token names.
  std::map<unsigned, DynSet> _turns;
  // The mode each node that leaves a reading in a cursor lexed, and its set.
  std::vector<std::pair<std::uint16_t, DynSet>> _reads;
  // The phrases that read a kept run, by root, with the function answering
  // each; the functions that read their runs, declared before the nodes that
  // call them and defined after the nodes they call.
  std::map<std::uint16_t, std::string> _holds;
  std::string _runDecls, _runDefs;
  std::vector<std::uint16_t> _rounds;
  unsigned _nparts = 0;
  std::uint32_t _nruns = 0;

  // The section whose text a compiled leaf ends with, zero outside any.
  unsigned home(unsigned l) const {
    const unsigned cfg = _g.gapLeaves[l].cfg;
    const unsigned h = cfg < _g.cfgHome.size() ? _g.cfgHome[cfg] : 0u;
    return h < _err.size() ? h : 0u;
  }

  // Whether every token of terminal `t` reads its spelling, up to case.
  bool literal(unsigned t) const {
    return _g.terms[t].spelled && !_g.termBuild[t].charset &&
           _g.terms[t].textLen != 0;
  }

  // What a head leaf of literal tokens answers of a token in hand of each
  // terminal, known from the terminal alone: 0 unknown, 1 false, 2 true; empty
  // where none is known. The leaf holds on its own terminals and fails on a
  // literal of another text, unless its skip reads trivia at the token's first
  // byte, which `trivia` holds for the reader to ask.
  std::vector<std::uint8_t> decided(const GapLeaf& leaf, ByteSet& trivia) const {
    if (leaf.kind != GapLeafKind::head || leaf.trigger) return {};
    const LexNode& root = _g.lex[leaf.node];
    std::vector<std::uint16_t> heads;
    if (root.kind == LexKind::token) heads.push_back(leaf.node);
    if (root.kind == LexKind::alt)
      for (unsigned i = 0; i < root.b; ++i)
        heads.push_back(_g.lexKid[root.a + i]);
    if (heads.empty() || std::ranges::any_of(heads, [&](std::uint16_t k) {
          return _g.lex[k].kind != LexKind::token;
        }))
      return {};
    if (leaf.skip != kNoLexNode) {
      const LexNode& skip = _g.lex[leaf.skip];
      for (unsigned i = 0; i < skip.b; ++i)
        trivia |= lex_first_bytes(
                      _g, _g.termBuild[_g.lex[_g.lexKid[skip.a + i]].a].lex)
                      .first;
    }
    const auto folded = [](std::string_view a, std::string_view b) {
      return std::ranges::equal(
          a, b, [](char x, char y) { return to_lower(x) == to_lower(y); });
    };
    std::vector<std::uint8_t> out(_g.nterms, 0);
    bool any = false;
    for (unsigned t = 1; t < _g.nterms; ++t) {
      if (const TermInfo& info = _g.terms[t]; info.trivia || info.laid != 0)
        continue;
      const std::string_view text = _g.term_text(static_cast<std::uint16_t>(t));
      ByteSet first;
      if (literal(t)) {
        const char c = to_lower(text[0]);
        first.add(static_cast<unsigned char>(c));
        if (c >= 'a' && c <= 'z')
          first.add(static_cast<unsigned char>(c - 'a' + 'A'));
      } else {
        first = lex_first_bytes(_g, _g.termBuild[t].lex).first;
      }
      if (!first.intersects(lex_not(trivia))) continue;
      bool own = false, other = literal(t);
      for (const std::uint16_t k : heads) {
        const unsigned x = _g.lex[k].a;
        own = own || x == t;
        other = other && literal(x) &&
                !folded(_g.term_text(static_cast<std::uint16_t>(x)), text);
      }
      if (own || other) {
        out[t] = own ? 2u : 1u;
        any = true;
      }
    }
    if (!any) out.clear();
    return out;
  }

  // Whether the phrase at `root` holds over `input`.
  std::string holds(std::uint16_t root, std::string_view input) const {
    const auto it = _holds.find(root);
    if (it == _holds.end()) return "!" + call(root, input) + ".empty()";
    return concat(it->second, "(src, len, ", input, ", seen)");
  }

  // A phrase that reads a run with no maximum outside any of its
  // repetitions reads it round by round, each round asking the rest of the
  // phrase, so that the parse keeps what each round answered.
  void keep_runs(std::uint16_t root) {
    if (_holds.contains(root) || !phrase_keeps_runs(_g, root)) return;
    const std::string name = concat("predicate_holds_", root);
    _holds.try_emplace(root, name);
    std::vector<std::uint16_t> parts;
    phrase_parts(_g, root, parts);
    rest_of(name, parts, 0, "::cursiv::predicate_some");
  }

  static std::string run_function(
      std::string_view name, std::string_view kind = "bool",
      std::string_view input = "::cursiv::PredicateState in") {
    return concat("inline ", kind, " ", name,
                  "(const char* src, ::cursiv::TextOffset len,\n"
                  "    ",
                  input, ", ::cursiv::TextOffset* seen)");
  }

  std::string prelex(std::uint16_t at, std::string_view indent) const {
    return concat(indent, "if (p.held == ::cursiv::kUnread) {\n", indent,
                  "  const auto sc = predicate_lex(src, len, p.at, ",
                  _g.lexMode[at], "u, seen);\n", indent,
                  "  p.held = sc.term; p.end = sc.end; p.mode = ",
                  _g.lexMode[at], "u;\n", indent, "}\n");
  }

  // Function `name`: whether `parts` from `k` and then `cont` hold.
  void rest_of(const std::string& name, const std::vector<std::uint16_t>& parts,
               std::size_t k, const std::string& cont) {
    const std::string head = run_function(name);
    _runDecls += head + ";\n";
    std::string body = head + " {\n  (void)src; (void)len; (void)seen;\n";
    for (std::size_t i = k; i < parts.size(); ++i) {
      const std::uint16_t at = parts[i];
      if (const bool run = phrase_kept_run(_g, at);
          run ||
          (_g.lex[at].kind == LexKind::alt && phrase_keeps_runs(_g, at))) {
        std::string rest = cont;
        if (i + 1 < parts.size()) {
          rest = concat("predicate_part_", _nparts++);
          rest_of(rest, parts, i + 1, cont);
        }
        _runDefs +=
            body + (run ? run_rounds(at, rest) : choice(at, rest)) + "}\n";
        return;
      }
      _called[at] = true;
      body += concat("  in = ", call(at, "::std::move(in)"),
                     ";\n  if (in.empty()) return false;\n");
    }
    _runDefs += concat(body, "  return ", cont,
                       "(src, len, ::std::move(in), seen);\n}\n");
  }

  // A choice whose arms read kept runs: each arm asks the rest itself, and
  // the others' cursors ask it together.
  std::string choice(std::uint16_t at, const std::string& rest) {
    const LexNode e = _g.lex[at];
    std::string arms;
    bool plain = false;
    for (unsigned i = 0; i < e.b; ++i) {
      const std::uint16_t arm = _g.lexKid[e.a + i];
      if (!phrase_keeps_runs(_g, arm)) {
        plain = true;
        _called[arm] = true;
        arms += concat("    plain.unite(",
                       call(arm, "::cursiv::PredicateState{p}"), ");\n");
        continue;
      }
      const std::string name = concat("predicate_part_", _nparts++);
      std::vector<std::uint16_t> parts;
      phrase_parts(_g, arm, parts);
      rest_of(name, parts, 0, rest);
      arms += concat("    holds = ", name,
                     "(src, len, ::cursiv::PredicateState{p}, seen) || "
                     "holds;\n");
    }
    return std::string(plain ? "  ::cursiv::PredicateState plain;\n" : "") +
           "  bool holds = false;\n"
           "  for (::std::size_t i = 0; i < in.size(); ++i) {\n"
           "    auto p = in[i];\n" +
           prelex(at, "    ") + arms + "  }\n" +
           (plain ? concat("  return ", rest,
                           "(src, len, ::std::move(plain), "
                           "seen) || holds;\n")
                  : "  return holds;\n");
  }

  // Repetition `at`, kept: the rounds it must read, then the free rounds the
  // parse keeps, each asking `rest`.
  std::string run_rounds(std::uint16_t at, const std::string& rest) {
    const LexNode e = _g.lex[at];
    const bool between = e.b != kNoLexNode;
    _called[e.a] = true;
    if (between) _called[e.b] = true;
    if (std::ranges::find(_rounds, at) == _rounds.end()) {
      _rounds.push_back(at);
      const std::string start = run_function(concat("predicate_start_", at),
                                             "::cursiv::PredicateCursor",
                                             "::cursiv::PredicateCursor p");
      const std::string step = run_function(concat("predicate_step_", at),
                                            "::cursiv::PredicateState");
      _runDecls += start + ";\n" + step + ";\n";
      _runDefs += start + " {\n";
      if (between)
        _runDefs += concat("  p = ", call(e.b, "::cursiv::PredicateState{p}"),
                           "[0];\n");
      _runDefs +=
          prelex(at, "  ") + "  return p;\n}\n" + step +
          concat(" {\n  return ", call(e.a, "::std::move(in)"), ";\n}\n");
    }
    const std::string free =
        concat("::cursiv::predicate_run(predicate_runs(src, len), ", _nruns++,
               "u,\n"
               "      ::cursiv::PredicateRound{&predicate_start_",
               at,
               ", "
               "&predicate_step_",
               at, ", ", between,
               "},\n"
               "      &",
               rest, ", src, len, ::std::move(in), seen)");
    // The rounds it must read before the free ones: every round but the
    // first reads the trivia between rounds.
    if (!between)
      return must_rounds("in", e.min, call(e.a, "in")) + "  return " + free +
             ";\n";
    if (e.min != 0)
      return concat("  in = ", call(e.a, "::std::move(in)"),
                    ";\n  if (in.empty()) return false;\n") +
             must_rounds("in", e.min - 1u, call(e.a, call(e.b, "in"))) +
             "  return " + free + ";\n";
    // No round at all: the cursors are read in the run's mode as they stand.
    return "  ::cursiv::PredicateState first;\n"
           "  for (::std::size_t i = 0; i < in.size(); ++i) {\n"
           "    auto p = in[i];\n" +
           prelex(at, "    ") + "    first.add(p);\n  }\n" +
           concat("  const bool none = ", rest,
                  "(src, len, first, seen);\n"
                  "  in = ",
                  call(e.a, "::std::move(first)"),
                  ";\n"
                  "  return ",
                  free, " || none;\n");
  }

  void visit(std::uint16_t at) {
    if (_used[at]) return;
    _used[at] = true;
    if (const LexNode e = _g.lex[at];
        e.kind == LexKind::seq || e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i) visit(_g.lexKid[e.a + i]);
    } else if (e.kind == LexKind::rep) {
      if (e.max != 0) visit(e.a);
      if (e.max > 1 && e.b != kNoLexNode) visit(e.b);
    } else if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
      visit(e.a);
    }
    _order.push_back(at);
  }

  void head_tokens(std::uint16_t at, bool turn) {
    const LexNode e = _g.lex[at];
    switch (e.kind) {
      case LexKind::token:
        _head[at] = true;
        _turn[at] = turn && turns_of(e.a).any();
        break;
      case LexKind::seq:
        for (unsigned i = 0; i < e.b; ++i) {
          head_tokens(_g.lexKid[e.a + i], turn);
          if (!_modes.nullable[_g.lexKid[e.a + i]]) break;
        }
        break;
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i)
          head_tokens(_g.lexKid[e.a + i], turn);
        break;
      case LexKind::rep:
        head_tokens(e.a, turn);
        break;
      default:
        break;
    }
  }

  std::string call(std::uint16_t at, std::string_view input) const {
    return concat("predicate_node_", at, "(src, len, ", input, ", seen)");
  }
  // `n` rounds of `step` from the cursors in `var`, up to one that moves none.
  static std::string must_rounds(std::string_view var, std::uint32_t n,
                                 const std::string& step) {
    if (n == 0) return {};
    return concat("  for (::std::uint32_t k = 0; k < ", n, "u && !", var,
                  ".empty(); ++k) {\n"
                  "    auto next = ",
                  step,
                  ";\n"
                  "    if (next == ",
                  var,
                  ") break;\n"
                  "    ",
                  var,
                  " = ::std::move(next);\n"
                  "  }\n");
  }
  // Whether `p` read nothing in a mode covering `want`: no read there finds
  // a token either.
  std::string read_nothing(const DynSet& want) const {
    std::string modes;
    std::vector<std::uint16_t> done;
    for (const auto& [mode, set] : _reads)
      if (set.covers(want) && std::ranges::find(done, mode) == done.end()) {
        modes += concat(modes.empty() ? "" : " || ", "p.mode == ", mode, "u");
        done.push_back(mode);
      }
    if (modes.empty()) return {};
    return "p.held == ::cursiv::kInvalidSymbol && (" + modes + ")";
  }
  bool alike(std::uint16_t x, std::uint16_t y) const {
    if (x == y) return true;
    const LexNode a = _g.lex[x], b = _g.lex[y];
    if (a.kind != b.kind || a.min != b.min || a.max != b.max) return false;
    switch (a.kind) {
      case LexKind::chars:
        return a.a == b.a;
      case LexKind::seq:
      case LexKind::alt:
        if (a.b != b.b) return false;
        for (unsigned i = 0; i < a.b; ++i)
          if (!alike(_g.lexKid[a.a + i], _g.lexKid[b.a + i])) return false;
        return true;
      case LexKind::rep:
        return alike(a.a, b.a) && a.b == b.b;
      case LexKind::nguard:
        if (a.a != b.a) return false;
        for (unsigned i = 0; i < a.min; ++i)
          if (_g.lexKid[a.b + i] != _g.lexKid[b.b + i]) return false;
        return true;
      default:
        return false;
    }
  }
  // The terminals the rule reads as `t`: itself, or for a spelling only a
  // condition names, the anonymous terminal a rule spells alike.
  const DynSet& turns_of(unsigned t) {
    const auto [at, fresh] = _turns.try_emplace(t);
    DynSet& out = at->second;
    if (!fresh) return out;
    if (!_g.termBuild[t].predicateOnly)
      out.set(t);
    else
      for (unsigned u = 1; u < _g.nterms; ++u)
        if (!_g.termBuild[u].predicateOnly &&
            _g.terms[u].nameOff == _g.terms[u].textOff &&
            alike(_g.termBuild[u].lex, _g.termBuild[t].lex))
          out.set(u);
    return out;
  }

  // The rounds of repetition `at` from `in`: `skip` read before each, an
  // exit before it, `item` the round.
  std::string rounds(std::uint16_t at, std::uint16_t skip, std::uint32_t min,
                     std::uint32_t max, const std::string& item) const {
    const LexNode e = _g.lex[at];
    std::string out =
        "  auto cur = ::std::move(in);\n" +
        must_rounds("cur", min,
                    call(e.a, skip == kNoLexNode ? "cur" : call(skip, "cur")));
    if (min == max) return out + "  return cur;\n}\n";
    out += "  auto visited = cur;\n";
    return out +
           concat(
               "  ::cursiv::PredicateState out;\n"
               "  for (::std::uint32_t k = ",
               min, "u; k < ", max,
               "u && !cur.empty(); ++k) "
               "{\n"
               "    ::cursiv::PredicateState active;\n"
               "    for (::std::size_t i = 0; i < cur.size(); ++i) {\n"
               "      auto p = ",
               skip == kNoLexNode
                   ? std::string("cur[i]")
                   : call(skip, "::cursiv::PredicateState{cur[i]}") + "[0]",
               ";\n", prelex(at, "      "), "      out.add(",
               skip == kNoLexNode ? "p" : "cur[i]",
               ");\n"
               "      active.add(p);\n"
               "    }\n"
               "    auto next = ",
               item,
               ";\n"
               "    cur = next.without(visited);\n"
               "    visited.unite(cur);\n"
               "  }\n"
               "  out.unite(cur);\n"
               "  return out;\n}\n");
  }

  std::string node(std::uint16_t at) const {
    const LexNode e = _g.lex[at];
    const bool owns = e.kind == LexKind::seq || e.kind == LexKind::rep;
    std::string out = concat(
        "inline ::cursiv::PredicateState predicate_node_", at,
        "(const char* src,\n"
        "    ::cursiv::TextOffset len, ",
        owns ? "::cursiv::PredicateState" : "const ::cursiv::PredicateState&",
        " in,\n"
        "    ::cursiv::TextOffset* seen) {\n"
        "  (void)src; (void)len; (void)seen;\n");
    if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i)
        out += concat("  in = ", call(_g.lexKid[e.a + i], "::std::move(in)"),
                      ";\n  if (in.empty()) return in;\n");
      return out + "  return in;\n}\n";
    }
    if (e.kind == LexKind::rep) {
      if (e.b == kNoLexNode || e.max <= 1)
        return out + rounds(at, kNoLexNode, e.min, e.max,
                            call(e.a, "::std::move(active)"));
      // Trivia stands between rounds: the first reads none, and the boundary
      // after the last is the caller's.
      const auto more = [&](std::string_view input) {
        return concat("predicate_rounds_", at, "(src, len, ", input, ", seen)");
      };
      std::string helper =
          concat("inline ::cursiv::PredicateState predicate_rounds_", at,
                 "(const char* "
                 "src,\n"
                 "    ::cursiv::TextOffset len, ::cursiv::PredicateState in,\n"
                 "    ::cursiv::TextOffset* seen) {\n"
                 "  (void)src; (void)len; (void)seen;\n");
      helper += rounds(at, e.b, e.min == 0 ? 0 : e.min - 1,
                       e.max == kEndless ? e.max : e.max - 1,
                       call(e.a, "::std::move(active)"));
      if (e.min == 0)
        return helper + out +
               rounds(at, kNoLexNode, 0, 1,
                      more(call(e.a, "::std::move(active)")));
      return helper + out +
             concat("  in = ", call(e.a, "::std::move(in)"),
                    ";\n  if (in.empty()) return in;\n  return ",
                    more("::std::move(in)"), ";\n}\n");
    }

    out +=
        "  ::cursiv::PredicateState out;\n"
        "  for (::std::size_t i = 0; i < in.size(); ++i) {\n"
        "    auto p = in[i];\n";
    switch (e.kind) {
      case LexKind::token: {
        DynSet self;
        self.set(e.a);
        if (const auto none = read_nothing(self); !none.empty())
          out += "    if (" + none + ") continue;\n";
        // The token in hand opens a condition: it holds where the terminal
        // reads its bytes, the lexer's choice aside, and a commit's exit where
        // the rule reads it as that terminal. Any other token is the
        // predicate's own, read under the alphabet it was lexed in.
        if (_turn[at])
          out += concat(
              "    if (p.held != ::cursiv::kUnread &&\n"
              "        p.held != ::cursiv::kInvalidSymbol) {\n"
              "      if ",
              gap_terms_expression(_g, _turns.at(e.a), "p.held"),
              " out.add({p.end, p.end});\n"
              "      continue;\n"
              "    }\n");
        out += concat(
            "    const auto sc = p.held == ", e.a,
            "u\n"
            "        ? ::cursiv::Scan{p.end, p.held}\n"
            "        : predicate_lex(src, len, p.at, ",
            _g.lexMode[at],
            "u, seen);\n"
            "    if (sc.term == ",
            e.a,
            "u && sc.end > p.at &&\n"
            "        (p.held == ::cursiv::kUnread ||\n"
            "         p.held == ::cursiv::kInvalidSymbol || (sc.end == p.end "
            "&&\n"
            "         (",
            _head[at] || _g.terms[e.a].spelled
                ? "true"
                : concat("p.held == ", e.a,
                         "u || p.mode == ::cursiv::kNoMode ||\n"
                         "          !predicate_reserved(src, len, p.at, p.end, "
                         "p.mode, ",
                         e.a, "u, seen)"),
            "))))\n"
            "      out.add({sc.end, sc.end});\n");
        break;
      }
      case LexKind::skip:
        if (e.b != 0) {
          DynSet trivia;
          for (unsigned i = 0; i < e.b; ++i)
            trivia |= _modes.first[_g.lexKid[e.a + i]];
          trivia.andnot(_modes.follow[at]);
          if (const auto none = read_nothing(_modes.want(at)); !none.empty())
            out += "    if (" + none +
                   ") {\n      out.add(p);\n      continue;\n    }\n";
          out += concat(
              "    for (;;) {\n"
              "      const auto sc = predicate_lex(src, len, p.at, ",
              _g.lexMode[at],
              "u, "
              "seen);\n"
              "      if (!",
              gap_terms_expression(_g, trivia, "sc.term"),
              " || sc.end <= p.at) {\n",
              concat("        if (p.held == ::cursiv::kUnread) {\n"
                     "          p.held = sc.term; p.end = sc.end; p.mode = ",
                     _g.lexMode[at],
                     "u;\n"
                     "        }\n"),
              "        break;\n"
              "      }\n"
              "      p.at = sc.end;\n"
              "      p.held = ::cursiv::kUnread;\n"
              "    }\n");
        }
        out += "    out.add(p);\n";
        break;
      case LexKind::alt:
        out += prelex(at, "    ");
        // A mode covering the whole choice covers each token: all would skip.
        if (std::ranges::all_of(std::span(_g.lexKid.data() + e.a, e.b),
                                [&](std::uint16_t k) {
                                  return _g.lex[k].kind == LexKind::token;
                                }))
          if (const auto none = read_nothing(_modes.want(at)); !none.empty())
            out += "    if (" + none + ") continue;\n";
        for (unsigned i = 0; i < e.b; ++i) {
          const auto kid = _g.lexKid[e.a + i];
          out += concat("      out.unite(",
                        call(kid, "::cursiv::PredicateState{p}"), ");\n");
        }
        break;
      case LexKind::positive:
      case LexKind::negative:
        out += concat(
            "    auto probe = p;\n"
            "    probe.held = ::cursiv::kUnread;\n"
            "    if (",
            e.kind == LexKind::positive ? "" : "!", "(",
            holds(e.a, "::cursiv::PredicateState{probe}"), ")) out.add(p);\n");
        break;
      case LexKind::gap_has:
      case LexKind::gap_is:
        out += concat("    if (",
                      _gaps.expression(e.a, e.kind == LexKind::gap_is,
                                       "src + p.from", "p.at - p.from"),
                      ") out.add(p);\n");
        break;
      case LexKind::end_mark:
        out +=
            "    if (seen) *seen = ::std::max(*seen, ::std::min(p.at + 1, "
            "len));\n";
        out += "    if (p.at == len) out.add(p);\n";
        break;
      default:
        throw std::logic_error(
            "cursiv: unexpected node in a generated predicate");
    }
    return out + "  }\n  return out;\n}\n";
  }
};

}  // namespace cursiv
