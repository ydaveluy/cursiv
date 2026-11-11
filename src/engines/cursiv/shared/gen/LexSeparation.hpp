// Tokens on either side of a trivia boundary must not match the same text.
// Caller-only trivia must not swallow a token's prefix either. First bytes
// decide most pairs; the existing token NFAs decide the rest, once per pair.
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LexAlphabet.hpp"
#include "cursiv/shared/gen/lex_prefix_overlap.hpp"
#include "cursiv/shared/table/DynSet.hpp"

namespace cursiv::engine {

template <class Cfg>
class LexSeparation {
 public:
  LexSeparation(const Cfg& g, const LexAlphabet& alphabet)
      : _g(g), _alphabet(alphabet) {}

  bool disjoint(const DynSet& a, const DynSet& b, bool prefixes = false) {
    if (a.none() || b.none()) return true;
    ByteSet x, y;
    if (!heads(a, x) || !heads(b, y)) return false;
    if (!x.intersects(y)) return true;
    for (const unsigned t : a.members())
      for (const unsigned u : b.members())
        if (_heads[t]->intersects(*_heads[u]) && !separated(t, u, prefixes))
          return false;
    return true;
  }

  // Whether one peek under the trivia of configuration `out` may stand for an
  // item of `first` read under `in`: it crosses every gap the item accepts,
  // and its own trivia hides no such item, not even its prefix.
  bool peek_safe(const DynSet& first, unsigned in, unsigned out) {
    DynSet own, caller;
    for (auto i = _g.cfgTriviaAt[in]; i < _g.cfgTriviaAt[in + 1]; ++i)
      own.set(_g.cfgTrivia[i]);
    for (auto i = _g.cfgTriviaAt[out]; i < _g.cfgTriviaAt[out + 1]; ++i)
      caller.set(_g.cfgTrivia[i]);
    if (!caller.covers(own)) return false;
    caller.andnot(own);
    return disjoint(first, caller, true);
  }

 private:
  const Cfg& _g;
  const LexAlphabet& _alphabet;
  std::vector<std::optional<ByteSet>> _heads;
  std::unordered_map<std::uint64_t, bool> _apart;

  bool heads(const DynSet& terms, ByteSet& out) {
    if (_heads.empty()) _heads.resize(_g.nterms);
    for (const unsigned t : terms.members()) {
      if (t >= _alphabet.startOf.size() || _alphabet.startOf[t] < 0)
        return false;
      auto& head = _heads[t];
      if (!head) head = lex_first_bytes(_g, _g.termBuild[t].lex).first;
      if (!head->any()) return false;
      out |= *head;
    }
    return true;
  }

  bool separated(unsigned a, unsigned b, bool prefixes) {
    if (a == b) return false;
    if (a > b) std::swap(a, b);
    const std::uint64_t key =
        std::uint64_t{prefixes} << 32 | std::uint64_t{a} << 16 | b;
    const auto [at, fresh] = _apart.try_emplace(key, false);
    if (fresh)
      at->second = lex_pair_search(
                       _alphabet.nfa, _g.sets.data(), _alphabet.startOf[a],
                       _alphabet.startOf[b],
                       [&](const LexWalkSide& x, const LexWalkSide& y) {
                         const bool first = lex_accepts(_alphabet.nfa, x.first);
                         const bool second =
                             lex_accepts(_alphabet.nfa, y.first);
                         return prefixes ? first || second : first && second;
                       }) == LexOverlap::none;
    return at->second;
  }
};

}  // namespace cursiv::engine
