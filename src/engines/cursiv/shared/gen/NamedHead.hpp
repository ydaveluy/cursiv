// What a head leaf answers of a token of a named terminal whose bytes a caret
// does not have: 0 false, 1 unknown, 2 true, read off the terminal's body. The
// leaf reads the whole token, so only the terminal that spells it holds.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

template <class Cfg>
class NamedHead {
 public:
  explicit NamedHead(const Cfg& g) : _g(g), _calls(lex_call_ancestors(g)) {}

  // A choice answers as the most its words answer, one by one.
  unsigned read(std::uint16_t root, unsigned t) const {
    const std::uint16_t body = _g.termBuild[t].lex;
    unsigned most = 0;
    for (std::vector todo{root}; !todo.empty();) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      const LexNode& e = _g.lex[at];
      if (e.kind == LexKind::token) {
        todo.push_back(lex_token_body(_g, e));
      } else if (e.kind == LexKind::alt) {
        for (unsigned i = 0; i < e.b; ++i) todo.push_back(_g.lexChild[e.a + i]);
      } else if (same_lex(at, body)) {
        most = 2;
      } else if (!_calls.empty() && _calls[body]) {
        most = std::max(most, 1u);
      } else if (const auto [first, empty] = lex_first_bytes(_g, body); empty) {
        most = std::max(most, 1u);
      } else {
        std::unordered_map<std::uint32_t, Reach> memo;
        const Reach r = descend(head_reach(at, Reach{2, 0}, first, memo));
        if (std::max(r.start, r.past) != 0) most = std::max(most, 1u);
      }
    }
    return most;
  }
  bool named(unsigned t) const {
    return !_g.terms[t].spelled && !_g.terms[t].trivia &&
           _g.terms[t].synthetic == 0;
  }
  // Whether some named terminal answers the leaf other than unknown.
  bool any(std::uint16_t root) const {
    const auto& answers = answers_of(root);
    return std::ranges::any_of(answers, [](std::uint8_t v) { return v != 1; });
  }
  // `unsigned name(SymbolId t)`, answering for each terminal.
  std::string source(std::string_view linkage, std::string_view name,
                     std::uint16_t root) const {
    TextBuilder out;
    out.line(linkage, " unsigned ", name,
             "(::cursiv::engine::SymbolId t) {\n  switch (t) {");
    const auto& answers = answers_of(root);
    for (const unsigned v : {0u, 2u}) {
      bool some = false;
      for (unsigned t = 1; t < _g.nterms; ++t)
        if (answers[t] == v) {
          out.line("    case ", t, ":");
          some = true;
        }
      if (some) out.line("      return ", v, "u;");
    }
    out.line("    default:\n      return 1u;\n  }\n}\n");
    return out.take();
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
    const auto same_set = [&](std::uint16_t p, std::uint16_t q) {
      return p == q ||
             (_g.sets[p] == _g.sets[q] && decodes(p) == decodes(q) &&
              (lex_set_scalars(_g, p) == lex_set_scalars(_g, q) ||
               lex_set_cps(_g, p).ranges == lex_set_cps(_g, q).ranges));
    };
    return lex_same_graph(_g, x, y, [&](const LexNode& e, const LexNode& f) {
      if (e.kind != f.kind || e.min != f.min || e.max != f.max) return false;
      switch (e.kind) {
        using enum LexKind;
        case chars:
          return same_set(e.a, f.a);
        case nguard:
          if ((e.a == kNoSet) != (f.a == kNoSet) ||
              (e.a != kNoSet && !same_set(e.a, f.a)))
            return false;
          for (std::uint32_t k = 0; k < e.min; ++k)
            if (!same_set(_g.lexChild[e.b + k], _g.lexChild[f.b + k]))
              return false;
          return true;
        case seq:
        case alt:
          return e.b == f.b;
        case rep:
          return true;
        default:
          return false;
      }
    });
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
  // `memo` keeps each node's answer from each `in` under one `first`.
  Descent<Reach> head_reach(
      std::uint16_t node, Reach in, const ByteSet& first,
      std::unordered_map<std::uint32_t, Reach>& memo) const {
    const std::uint32_t key =
        std::uint32_t{node} << 8 | in.start << 4 | in.past;
    if (const auto it = memo.find(key); it != memo.end()) co_return it->second;
    const LexNode& e = _g.lex[node];
    const unsigned beyond = std::min(in.past, 1u);
    Reach out{std::min(in.start, 1u), std::max(std::min(in.start, 1u), beyond)};
    switch (e.kind) {
      case LexKind::chars: {
        const ByteSet& set = _g.sets[e.a];
        const unsigned from = set.covers(first) ? in.start
                              : first.intersects(set | decodes(e.a))
                                  ? std::min(in.start, 1u)
                                  : 0u;
        out = {0, std::max(from, beyond)};
        break;
      }
      case LexKind::nguard: {
        // Refused at the start where the word's first class takes every
        // first byte and nothing lifts a word of one class.
        unsigned start = std::min(in.start, 1u);
        if (e.min != 0) {
          const std::uint16_t c0 = _g.lexChild[e.b];
          if (!first.intersects(_g.sets[c0] | decodes(c0)))
            start = in.start;
          else if (e.min == 1 && e.a == kNoSet && _g.sets[c0].covers(first))
            start = 0;
        }
        out = {start, beyond};
        break;
      }
      case LexKind::seq:
        out = in;
        for (unsigned i = 0; i < e.b; ++i)
          out = co_await head_reach(_g.lexChild[e.a + i], out, first, memo);
        break;
      case LexKind::alt:
        out = {};
        for (unsigned i = 0; i < e.b; ++i)
          out = widen(
              out, co_await head_reach(_g.lexChild[e.a + i], in, first, memo));
        break;
      case LexKind::rep: {
        // An cycle longer than the nine values repeats one.
        constexpr unsigned kMostValues = 9;
        Reach cur = in;
        bool cycles = false;
        for (std::uint32_t k = 0; k < e.min; ++k) {
          const Reach next = co_await head_reach(e.a, cur, first, memo);
          if (next == cur) break;
          if (k == kMostValues) {
            cycles = true;
            break;
          }
          cur = next;
        }
        if (cycles) {
          out = {1, 1};
          break;
        }
        out = cur;
        for (std::uint32_t k = 0;
             k < kMostValues && (e.max == 0xffffffffu || k < e.max - e.min);
             ++k) {
          cur = co_await head_reach(e.a, cur, first, memo);
          out = widen(out, cur);
        }
        break;
      }
      default:
        break;
    }
    memo.emplace(key, out);
    co_return out;
  }

  const Cfg& _g;
  std::vector<std::uint8_t> _calls;
  mutable std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> _answers;
};

}  // namespace cursiv::engine
