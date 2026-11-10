// The chain pool while it is being built: a record names a run of actions,
// and one run, wherever it is used, is one entry.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/shared/gen/ChainIndex.hpp"
#include "cursiv/shared/gen/Vec.hpp"
#include "cursiv/shared/gen/bit_words.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

// A run of actions, end to end in one pool. A record carries the chain of its
// production and its call sites, never a single action: a rule that writes no
// record of its own gives its actions to the record below.
struct Chain {
  std::uint16_t off = 0, count = 0;
};

// What construction concludes about a chain and no run ever asks: whether it
// hands a node back, whether it opens one, and the write a caret reads it by.
struct ChainBuild {
  bool makes = false;
  bool creates = false;
  // The first action that writes a member. An optional creates a chain per
  // branch, so several chains write one member; they start with the same
  // write, which is all a caret asks of any of them.
  std::uint16_t lead = kNoRow;
  // A nest turn's twin of the same run: its record reaches back over its
  // receiver, which the tree builder must tell from one that does not.
  bool turn = false;
  // The hover text of the token the chain stands on, where that use of the word
  // has one. Two uses with different texts are two chains.
  std::string_view doc{};
};

// The cap a build names where the pool is `capped`.
inline constexpr const char* kTooManyChains =
    "its AST writes need more than 65 535 chains or actions";

struct ChainPool {
  Vec<std::uint16_t> acts{};
  std::uint16_t nacts = 0;
  Vec<Chain> chains{};
  Vec<ChainBuild> chainBuild{};
  std::uint16_t nchains = 0;
  // The pools grow as needed, so only the id is bounded. The build reads this
  // flag once and reports the cap: a count that wrapped would give two chains
  // one id.
  bool capped = false;

  // A chain is what it says, not where it stands: the same run of actions,
  // wherever it is used, is one entry.
  std::uint16_t intern_chain(const std::uint16_t* a, std::size_t n,
                             std::string_view doc = {}) {
    return n == 0 && doc.empty() ? kNoRow : intern(a, n, false, doc);
  }

  // A record that says nothing, and is still a record: a call site's
  // directive needs a span, and a production whose body opens none gives it
  // one.
  std::uint16_t bare_chain() { return intern(nullptr, 0); }

  // The two runs standing over one position, interned as one. A joined run is
  // built only where both hold something, which is the rare case.
  std::uint16_t intern_cat(const std::vector<std::uint16_t>& a,
                           const std::vector<std::uint16_t>& b,
                           std::string_view doc = {}) {
    if (b.empty()) return intern_chain(a.data(), a.size(), doc);
    if (a.empty()) return intern_chain(b.data(), b.size(), doc);
    return intern_joined(a, b, doc);
  }

  // The same run as a turn's own chain.
  std::uint16_t turn_chain(std::uint16_t c) {
    if (c == kNoRow) return kNoRow;
    const Chain x = chains[c];
    const std::vector<std::uint16_t> run(acts.data() + x.off,
                                         acts.data() + x.off + x.count);
    return intern(run.data(), run.size(), true, chainBuild[c].doc);
  }

  // A chain opens a record when it starts with a creation, and hands a node
  // back when it starts with a creation or a close.
  void note_makes(std::span<const WriteKind> kind) {
    for (std::uint16_t i = 0; i < nchains; ++i) {
      const Chain& ch = chains[i];
      // Every chain has a build half, the empty ones included: the pool is
      // indexed, so a hole in it is a read past the end.
      ChainBuild& cb = chainBuild[i];
      if (ch.count == 0) continue;  // a record that says nothing opens nothing
      const WriteKind first = kind[acts[ch.off]];
      cb.creates = first == WriteKind::create || first == WriteKind::climb;
      cb.makes = cb.creates || first == WriteKind::close;
    }
  }

  // The text a join of two chains keeps: only a token's own chain has one.
  std::string_view doc_of(std::uint16_t a, std::uint16_t b) const {
    return chainBuild[b].doc.empty() ? chainBuild[a].doc : chainBuild[b].doc;
  }

  // One chain after another, as one: what a production hands to the record
  // below it comes first, what the call site adds comes after.
  std::uint16_t concat(std::uint16_t a, std::uint16_t b) {
    if (a == kNoRow) return b;
    if (b == kNoRow) return a;
    const Chain x = chains[a], y = chains[b];
    const std::uint16_t* const ac = acts.data();
    return intern_joined({ac + x.off, x.count}, {ac + y.off, y.count},
                         doc_of(a, b));
  }

 private:
  ChainIndex _index{};

  // The run and its text: the probe compares `turn`.
  static std::uint64_t hash_run(const std::uint16_t* a, std::size_t n,
                                std::string_view doc) {
    std::uint64_t h = hash_mix(0x9e3779b97f4a7c15ull, n);
    for (std::size_t k = 0; k < n; ++k) h = hash_mix(h, a[k]);
    return doc.empty() ? h : hash_mix(h, std::hash<std::string_view>{}(doc));
  }

  std::uint16_t intern_joined(std::span<const std::uint16_t> a,
                              std::span<const std::uint16_t> b,
                              std::string_view doc) {
    std::vector<std::uint16_t> j(a.begin(), a.end());
    j.insert(j.end(), b.begin(), b.end());
    return intern(j.data(), j.size(), false, doc);
  }

  std::uint16_t intern(const std::uint16_t* a, std::size_t n,
                       bool turn = false, std::string_view doc = {}) {
    if (_index.head.empty()) _index.init(8);
    const std::uint64_t h = hash_run(a, n, doc);
    const Chain* const ch = chains.data();
    const std::uint16_t* const ac = acts.data();
    for (unsigned i = _index.first(h); i != ChainIndex::kNone;
         i = _index.after(i))
      if (ch[i].count == n && chainBuild[i].turn == turn &&
          chainBuild[i].doc == doc && std::equal(a, a + n, ac + ch[i].off))
        return static_cast<std::uint16_t>(i);
    // A chain's id is its count, on sixteen bits: a count that wraps would
    // hand two chains one id.
    if (nchains == kNoElement || nacts + n > 0xFFFFu) {
      capped = true;
      return kNoRow;
    }
    const std::uint16_t i = nchains++;
    _index.add(h, i);
    chains[i] = Chain{nacts, static_cast<std::uint16_t>(n)};
    chainBuild[i] = {.turn = turn, .doc = doc};  // the two pools are one length
    std::copy_n(a, n, acts.row(nacts, n));
    nacts = static_cast<std::uint16_t>(nacts + n);
    return i;
  }
};

}  // namespace cursiv::engine
