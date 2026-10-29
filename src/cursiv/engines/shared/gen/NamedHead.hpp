// What a head leaf answers of a token of a named terminal whose bytes a caret
// does not have: 0 false, 1 unknown, 2 true, read off the terminal's body. The
// leaf reads the whole token, so only the terminal that spells it holds.
#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <class Cfg>
class NamedHead {
 public:
  explicit NamedHead(const Cfg& g) : _g(g), _calls(lex_call_ancestors(g)) {}

  // A choice answers as the most its words answer, one by one.
  unsigned read(std::uint16_t root, unsigned t) const {
    const LexNode& e = _g.lex[root];
    if (e.kind == LexKind::token) return read(lex_token_body(_g, e), t);
    if (e.kind == LexKind::alt) {
      unsigned most = 0;
      for (unsigned i = 0; i < e.b; ++i)
        most = std::max(most, read(_g.lexKid[e.a + i], t));
      return most;
    }
    const std::uint16_t body = _g.termBuild[t].lex;
    if (same_lex(root, body)) return 2;
    if (!_calls.empty() && _calls[body]) return 1;
    const auto [first, empty] = lex_first_bytes(_g, body);
    if (empty) return 1;
    const Reach r = head_reach(root, Reach{2, 0}, first);
    return std::max(r.start, r.past) == 0 ? 0u : 1u;
  }
  bool named(unsigned t) const {
    return !_g.terms[t].spelled && !_g.terms[t].trivia && _g.terms[t].laid == 0;
  }
  // Whether some named terminal answers the leaf other than unknown.
  bool any(std::uint16_t root) const {
    const auto& answers = answers_of(root);
    return std::ranges::any_of(answers, [](std::uint8_t v) { return v != 1; });
  }
  // `unsigned name(::cursiv::SymbolId t)`, answering for each terminal.
  std::string source(std::string_view linkage, std::string_view name,
                     std::uint16_t root) const {
    std::string out = std::format(
        "{} unsigned {}(::cursiv::SymbolId t) {{\n  switch (t) {{\n", linkage,
        name);
    const auto& answers = answers_of(root);
    for (const unsigned v : {0u, 2u}) {
      bool some = false;
      for (unsigned t = 1; t < _g.nterms; ++t)
        if (answers[t] == v) {
          out += std::format("    case {}:\n", t);
          some = true;
        }
      if (some) out += std::format("      return {}u;\n", v);
    }
    out += "    default:\n      return 1u;\n  }\n}\n\n";
    return out;
  }

 private:
  ByteSet decodes(std::uint16_t set) const { return lex_decode_bytes(_g, set); }

  // Per terminal, 1 where it is not named.
  const std::vector<std::uint8_t>& answers_of(std::uint16_t root) const {
    const auto [at, fresh] = _answers.try_emplace(root);
    if (fresh) {
      at->second.assign(_g.nterms, 1);
      for (unsigned t = 1; t < _g.nterms; ++t)
        if (named(t)) at->second[t] = static_cast<std::uint8_t>(read(root, t));
    }
    return at->second;
  }

  bool same_lex(std::uint16_t x, std::uint16_t y) const {
    if (x == y) return true;
    const LexNode& e = _g.lex[x];
    const LexNode& f = _g.lex[y];
    if (e.kind != f.kind || e.min != f.min || e.max != f.max) return false;
    const auto same_set = [&](std::uint16_t p, std::uint16_t q) {
      return p == q ||
             (_g.sets[p] == _g.sets[q] && decodes(p) == decodes(q) &&
              (lex_set_scalars(_g, p) == lex_set_scalars(_g, q) ||
               lex_set_cps(_g, p).ranges == lex_set_cps(_g, q).ranges));
    };
    switch (e.kind) {
      case LexKind::chars:
        return same_set(e.a, f.a);
      case LexKind::nguard:
        if ((e.a == kNoSet) != (f.a == kNoSet) ||
            (e.a != kNoSet && !same_set(e.a, f.a)))
          return false;
        for (std::uint32_t k = 0; k < e.min; ++k)
          if (!same_set(_g.lexKid[e.b + k], _g.lexKid[f.b + k])) return false;
        return true;
      case LexKind::seq:
      case LexKind::alt:
        if (e.b != f.b) return false;
        for (unsigned i = 0; i < e.b; ++i)
          if (!same_lex(_g.lexKid[e.a + i], _g.lexKid[f.a + i])) return false;
        return true;
      case LexKind::rep:
        return same_lex(e.a, f.a);
      default:
        return false;
    }
  }
  // Where a head expression stands over a token whose first byte lies in
  // `first`: at the start, or past it, each never, maybe or always.
  struct Reach {
    unsigned start = 0, past = 0;
    bool operator==(const Reach&) const = default;
  };
  static Reach widen(Reach x, Reach y) {
    return {std::max(x.start, y.start), std::max(x.past, y.past)};
  }
  Reach head_reach(std::uint16_t node, Reach in, const ByteSet& first) const {
    const LexNode& e = _g.lex[node];
    const unsigned beyond = std::min(in.past, 1u);
    switch (e.kind) {
      case LexKind::chars: {
        const ByteSet& set = _g.sets[e.a];
        const unsigned from = set.covers(first) ? in.start
                              : first.intersects(set | decodes(e.a))
                                  ? std::min(in.start, 1u)
                                  : 0u;
        return {0, std::max(from, beyond)};
      }
      case LexKind::nguard: {
        // Refused at the start where the word's first class takes every
        // first byte and nothing rescues a word of one class.
        unsigned start = std::min(in.start, 1u);
        if (e.min != 0) {
          const std::uint16_t c0 = _g.lexKid[e.b];
          if (!first.intersects(_g.sets[c0] | decodes(c0)))
            start = in.start;
          else if (e.min == 1 && e.a == kNoSet && _g.sets[c0].covers(first))
            start = 0;
        }
        return {start, beyond};
      }
      case LexKind::seq: {
        Reach cur = in;
        for (unsigned i = 0; i < e.b; ++i)
          cur = head_reach(_g.lexKid[e.a + i], cur, first);
        return cur;
      }
      case LexKind::alt: {
        Reach out{};
        for (unsigned i = 0; i < e.b; ++i)
          out = widen(out, head_reach(_g.lexKid[e.a + i], in, first));
        return out;
      }
      case LexKind::rep: {
        // An orbit longer than the nine values repeats one.
        constexpr unsigned kOrbit = 9;
        Reach cur = in;
        for (std::uint32_t k = 0; k < e.min; ++k) {
          const Reach next = head_reach(e.a, cur, first);
          if (next == cur) break;
          if (k == kOrbit) return {1, 1};
          cur = next;
        }
        Reach out = cur;
        for (std::uint32_t k = 0;
             k < kOrbit && (e.max == 0xffffffffu || k < e.max - e.min); ++k) {
          cur = head_reach(e.a, cur, first);
          out = widen(out, cur);
        }
        return out;
      }
      default:
        return {std::min(in.start, 1u),
                std::max(std::min(in.start, 1u), beyond)};
    }
  }

  const Cfg& _g;
  std::vector<std::uint8_t> _calls;
  mutable std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> _answers;
};

}  // namespace cursiv
