// Tokens on either side of a trivia boundary must not match the same text.
// Caller-only trivia must not swallow a token's prefix either. First bytes
// settle most pairs; the existing token NFAs settle the rest, once per pair.
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexAlphabet.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

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
    bool apart = true;
    each_bit(a.w.data(), a.w.size(), [&](std::size_t t) {
      if (!apart) return;
      each_bit(b.w.data(), b.w.size(), [&](std::size_t u) {
        if (apart && _heads[t]->intersects(*_heads[u]))
          apart = separated(static_cast<unsigned>(t), static_cast<unsigned>(u),
                            prefixes);
      });
    });
    return apart;
  }

 private:
  const Cfg& _g;
  const LexAlphabet& _alphabet;
  std::vector<std::optional<ByteSet>> _heads;
  std::unordered_map<std::uint64_t, bool> _apart;

  bool heads(const DynSet& terms, ByteSet& out) {
    if (_heads.empty()) _heads.resize(_g.nterms);
    bool known = true;
    each_bit(terms.w.data(), terms.w.size(), [&](std::size_t t) {
      if (t >= _alphabet.startOf.size() || _alphabet.startOf[t] < 0) {
        known = false;
        return;
      }
      auto& head = _heads[t];
      if (!head) head = lex_first_bytes(_g, _g.termBuild[t].lex).first;
      known = known && head->any();
      out |= *head;
    });
    return known;
  }

  bool separated(unsigned a, unsigned b, bool prefixes) {
    if (a == b) return false;
    if (a > b) std::swap(a, b);
    const std::uint64_t key =
        std::uint64_t{prefixes} << 32 | std::uint64_t{a} << 16 | b;
    const auto [at, fresh] = _apart.try_emplace(key, false);
    if (fresh)
      at->second =
          lex_pair_search(
              _alphabet.nfa, _g.sets.data(), _alphabet.startOf[a],
              _alphabet.startOf[b],
              [&](const std::vector<int>& x, const std::vector<int>& y) {
                const bool first = lex_accepts(_alphabet.nfa, x);
                const bool second = lex_accepts(_alphabet.nfa, y);
                return prefixes ? first || second : first && second;
              }) == LexOverlap::none;
    return at->second;
  }
};

}  // namespace cursiv
