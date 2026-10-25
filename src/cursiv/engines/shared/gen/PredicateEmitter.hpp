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

#include "cursiv/engines/shared/gen/GapConditionEmitter.hpp"
#include "cursiv/engines/shared/gen/GapEmitter.hpp"
#include "cursiv/engines/shared/gen/PredicateModes.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"

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
  }

  std::string declarations() const {
    std::string out;
    for (unsigned l = 0; l < _g.ngapLeaves; ++l)
      if (compiled(l))
        out += std::format(
            "inline bool predicate_{}(const char*, ::cursiv::TextOffset,\n"
            "    const ::cursiv::Token&, ::cursiv::TextOffset,\n"
            "    ::cursiv::TextOffset* = nullptr);\n",
            l);
    return out;
  }

  bool compiled(unsigned l) const { return _g.gapLeaves[l].reads_tokens(); }

  std::string source(std::string_view lexer, std::string_view tape) const {
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
    if (lexed || sectioned) {
      out +=
          "inline ::cursiv::Scan predicate_lex(const char* src,\n"
          "    ::cursiv::TextOffset len, ::cursiv::TextOffset at,\n"
          "    unsigned mode, ::cursiv::TextOffset* seen) {\n";
      out += std::format(
          "  auto tape = {};\n"
          "  return ::cursiv::LexSuffixSession<decltype(tape)>::match(\n"
          "      tape, src, len, mode, at, seen, [&] {{\n",
          tape);
      out += lexer;
      out += "  });\n}\n";
    }
    if (reserved) {
      out += std::format(
          "inline bool predicate_reserved(const char* src, "
          "::cursiv::TextOffset len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset end,\n"
          "    unsigned mode, ::cursiv::SymbolId term, ::cursiv::TextOffset* "
          "seen) {{\n"
          "  const auto tape = {};\n"
          "  auto state = tape.start(mode);\n"
          "  for (auto i = at; i < end; ++i) {{\n"
          "    state = tape.step(state, static_cast<unsigned char>(src[i]));\n"
          "    if (state == ::cursiv::kDeadState) return false;\n"
          "  }}\n"
          "  if (!kTable.dfa_res_set(state).test(term)) return false;\n"
          "  auto far = end;\n"
          "  const bool reserved = ::cursiv::lex_reserves<kTable>(\n"
          "      state, src, end, len, far, [&](::cursiv::SymbolId t) {{\n"
          "        return !tape.refuses(t, src + at, len - at);\n"
          "      }});\n"
          "  if (seen) *seen = ::std::max(*seen, far);\n"
          "  return reserved;\n"
          "}}\n",
          tape);
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
    for (const auto at : _order) out += node(at);
    for (unsigned l = 0; l < _g.ngapLeaves; ++l) {
      if (!compiled(l)) continue;
      const GapLeaf& leaf = _g.gapLeaves[l];
      const std::string start =
          "::cursiv::PredicateState{::cursiv::PredicateCursor{at, "
          "::std::min(from, at),\n"
          "      la.end > la.begin ? la.sym : ::cursiv::kUnread, la.mode, "
          "la.end}}";
      const unsigned h = home(l);
      out += std::format(
          "inline bool predicate_{}{}(const char* src, ::cursiv::TextOffset "
          "len,\n"
          "    const ::cursiv::Token& la, ::cursiv::TextOffset from,\n"
          "    ::cursiv::TextOffset* seen) {{\n"
          "  const auto at = ::std::min(la.begin, len);\n"
          "  return !{}.empty();\n"
          "}}\n",
          l, h != 0 ? "_in" : "",
          call(leaf.node,
               leaf.skip == kNoLexNode ? start : call(leaf.skip, start)));
      // A section's predicate reads its text alone: one that looked as far as
      // the token closing the section is read again up to there.
      if (h != 0)
        out += std::format(
            "inline bool predicate_{0}(const char* src, ::cursiv::TextOffset "
            "len,\n"
            "    const ::cursiv::Token& la, ::cursiv::TextOffset from,\n"
            "    ::cursiv::TextOffset* seen) {{\n"
            "  ::cursiv::TextOffset reach = 0;\n"
            "  const bool value = predicate_{0}_in(src, len, la, from, "
            "&reach);\n"
            "  const auto close = predicate_close(src, len, "
            "::std::min(la.begin, len),\n"
            "      reach, {1}u, {2}u, &reach);\n"
            "  if (seen) *seen = ::std::max(*seen, reach);\n"
            "  return close < reach\n"
            "      ? predicate_{0}_in(src, close, la, from, seen) : value;\n"
            "}}\n",
            l, _err[h], _g.cfgEnd[h]);
    }
    return out;
  }

 private:
  const Cfg& _g;
  const GapEmitter<Cfg>& _gaps;
  std::span<const std::uint16_t> _err;
  PredicateModes<Cfg> _modes;
  std::vector<bool> _used;
  // The tokens a condition opens with, which read the token in hand, and those
  // of a commit's exit the rule has a terminal for.
  std::vector<bool> _head, _turn;
  std::vector<std::uint16_t> _order;
  // `turn_terms` of each terminal a turn token names.
  std::map<unsigned, DynSet> _turns;
  // The mode each node that leaves a reading in a cursor lexed, and its set.
  std::vector<std::pair<std::uint16_t, DynSet>> _reads;

  // The section whose text a compiled leaf ends with, zero outside any.
  unsigned home(unsigned l) const {
    const unsigned cfg = _g.gapLeaves[l].cfg;
    const unsigned h = cfg < _g.cfgHome.size() ? _g.cfgHome[cfg] : 0u;
    return h < _err.size() ? h : 0u;
  }

  void visit(std::uint16_t at) {
    if (_used[at]) return;
    _used[at] = true;
    const LexNode e = _g.lex[at];
    if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
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
    return std::format("predicate_node_{}(src, len, {}, seen)", at, input);
  }
  // Whether `p` read nothing in a mode covering `want`: no read there finds
  // a token either.
  std::string read_nothing(const DynSet& want) const {
    std::string modes;
    std::vector<std::uint16_t> done;
    for (const auto& [mode, set] : _reads)
      if (set.covers(want) && std::ranges::find(done, mode) == done.end()) {
        modes +=
            std::format("{}p.mode == {}u", modes.empty() ? "" : " || ", mode);
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
  const DynSet& turns_of(unsigned t) {
    const auto [at, fresh] = _turns.try_emplace(t);
    if (fresh) at->second = turn_terms(t);
    return at->second;
  }
  // The terminals the rule reads as `t`: itself, or for a spelling only a
  // condition names, the anonymous terminal a rule spells alike.
  DynSet turn_terms(unsigned t) const {
    DynSet out;
    if (!_g.termBuild[t].predicateOnly) {
      out.set(t);
      return out;
    }
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
    const auto skipped = [&](std::string_view input) {
      return skip == kNoLexNode ? std::string(input) : call(skip, input);
    };
    std::string out = "  auto cur = ::std::move(in);\n";
    if (min != 0)
      out += std::format(
          "  for (::std::uint32_t k = 0; k < {}u && !cur.empty(); ++k) {{\n"
          "    auto next = {};\n"
          "    if (next == cur) break;\n"
          "    cur = ::std::move(next);\n"
          "  }}\n",
          min, call(e.a, skipped("cur")));
    if (min == max) return out + "  return cur;\n}\n";
    out += "  auto visited = cur;\n";
    return out +
           std::format(
               "  ::cursiv::PredicateState out;\n"
               "  for (::std::uint32_t k = {}u; k < {}u && !cur.empty(); ++k) "
               "{{\n"
               "    ::cursiv::PredicateState active;\n"
               "    for (::std::size_t i = 0; i < cur.size(); ++i) {{\n"
               "      auto p = {};\n"
               "      if (p.held == ::cursiv::kUnread) {{\n"
               "        const auto sc = predicate_lex(src, len, p.at, {}u, "
               "seen);\n"
               "        p.held = sc.term; p.end = sc.end; p.mode = {}u;\n"
               "      }}\n"
               "      out.add({});\n"
               "      active.add(p);\n"
               "    }}\n"
               "    auto next = {};\n"
               "{}"
               "  }}\n"
               "  out.unite(cur);\n"
               "  return out;\n}}\n",
               min, max,
               skip == kNoLexNode
                   ? std::string("cur[i]")
                   : call(skip, "::cursiv::PredicateState{cur[i]}") + "[0]",
               _g.lexMode[at], _g.lexMode[at],
               skip == kNoLexNode ? "p" : "cur[i]", item,
               "    cur = next.without(visited);\n"
               "    visited.unite(cur);\n");
  }

  std::string node(std::uint16_t at) const {
    const LexNode e = _g.lex[at];
    const bool owns = e.kind == LexKind::seq || e.kind == LexKind::rep;
    std::string out = std::format(
        "inline ::cursiv::PredicateState predicate_node_{}(const char* src,\n"
        "    ::cursiv::TextOffset len, {} in,\n"
        "    ::cursiv::TextOffset* seen) {{\n"
        "  (void)src; (void)len; (void)seen;\n",
        at,
        owns ? "::cursiv::PredicateState" : "const ::cursiv::PredicateState&");
    if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i)
        out += std::format("  in = {};\n  if (in.empty()) return in;\n",
                           call(_g.lexKid[e.a + i], "::std::move(in)"));
      return out + "  return in;\n}\n";
    }
    if (e.kind == LexKind::rep) {
      if (e.b == kNoLexNode || e.max <= 1)
        return out + rounds(at, kNoLexNode, e.min, e.max,
                            call(e.a, "::std::move(active)"));
      // Trivia stands between rounds: the first reads none, and the boundary
      // after the last is the caller's.
      const auto more = [&](std::string_view input) {
        return std::format("predicate_rounds_{}(src, len, {}, seen)", at,
                           input);
      };
      std::string helper = std::format(
          "inline ::cursiv::PredicateState predicate_rounds_{}(const char* "
          "src,\n"
          "    ::cursiv::TextOffset len, ::cursiv::PredicateState in,\n"
          "    ::cursiv::TextOffset* seen) {{\n"
          "  (void)src; (void)len; (void)seen;\n",
          at);
      helper += rounds(at, e.b, e.min == 0 ? 0 : e.min - 1,
                       e.max == 0xffffffffu ? e.max : e.max - 1,
                       call(e.a, "::std::move(active)"));
      if (e.min == 0)
        return helper + out +
               rounds(at, kNoLexNode, 0, 1,
                      more(call(e.a, "::std::move(active)")));
      return helper + out +
             std::format(
                 "  in = {};\n  if (in.empty()) return in;\n  return {};\n}}\n",
                 call(e.a, "::std::move(in)"), more("::std::move(in)"));
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
          out += std::format(
              "    if (p.held != ::cursiv::kUnread &&\n"
              "        p.held != ::cursiv::kInvalidSymbol) {{\n"
              "      if {} out.add({{p.end, p.end}});\n"
              "      continue;\n"
              "    }}\n",
              gap_terms_expression(_g, _turns.at(e.a), "p.held"));
        out += std::format(
            "    const auto sc = p.held == {}u\n"
            "        ? ::cursiv::Scan{{p.end, p.held}}\n"
            "        : predicate_lex(src, len, p.at, {}u, seen);\n"
            "    if (sc.term == {}u && sc.end > p.at &&\n"
            "        (p.held == ::cursiv::kUnread ||\n"
            "         p.held == ::cursiv::kInvalidSymbol || (sc.end == p.end "
            "&&\n"
            "         ({}))))\n"
            "      out.add({{sc.end, sc.end}});\n",
            e.a, _g.lexMode[at], e.a,
            _head[at] || _g.terms[e.a].spelled
                ? "true"
                : std::format(
                      "p.held == {0}u || p.mode == ::cursiv::kNoMode ||\n"
                      "          !predicate_reserved(src, len, p.at, p.end, "
                      "p.mode, {0}u, seen)",
                      e.a));
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
          out += std::format(
              "    for (;;) {{\n"
              "      const auto sc = predicate_lex(src, len, p.at, {}u, "
              "seen);\n"
              "      if (!{} || sc.end <= p.at) {{\n"
              "{}"
              "        break;\n"
              "      }}\n"
              "      p.at = sc.end;\n"
              "      p.held = ::cursiv::kUnread;\n"
              "    }}\n",
              _g.lexMode[at], gap_terms_expression(_g, trivia, "sc.term"),
              std::format(
                  "        if (p.held == ::cursiv::kUnread) {{\n"
                  "          p.held = sc.term; p.end = sc.end; p.mode = {}u;\n"
                  "        }}\n",
                  _g.lexMode[at]));
        }
        out += "    out.add(p);\n";
        break;
      case LexKind::alt:
        out += std::format(
            "    if (p.held == ::cursiv::kUnread) {{\n"
            "      const auto sc = predicate_lex(src, len, p.at, {0}u, seen);\n"
            "      p.held = sc.term;\n"
            "      p.end = sc.end;\n"
            "      p.mode = {0}u;\n"
            "    }}\n",
            _g.lexMode[at]);
        // A mode covering the whole choice covers each token: all would skip.
        if (std::ranges::all_of(std::span(_g.lexKid.data() + e.a, e.b),
                                [&](std::uint16_t k) {
                                  return _g.lex[k].kind == LexKind::token;
                                }))
          if (const auto none = read_nothing(_modes.want(at)); !none.empty())
            out += "    if (" + none + ") continue;\n";
        for (unsigned i = 0; i < e.b; ++i) {
          const auto kid = _g.lexKid[e.a + i];
          out += std::format("      out.unite({});\n",
                             call(kid, "::cursiv::PredicateState{p}"));
        }
        break;
      case LexKind::positive:
      case LexKind::negative:
        out += std::format(
            "    auto probe = p;\n"
            "    probe.held = ::cursiv::kUnread;\n"
            "    if ({}.empty() {}) out.add(p);\n",
            call(e.a, "::cursiv::PredicateState{probe}"),
            e.kind == LexKind::positive ? "== false" : "== true");
        break;
      case LexKind::gap_has:
      case LexKind::gap_is:
        out += std::format("    if ({}) out.add(p);\n",
                           _gaps.expression(e.a, e.kind == LexKind::gap_is,
                                            "src + p.from", "p.at - p.from"));
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
