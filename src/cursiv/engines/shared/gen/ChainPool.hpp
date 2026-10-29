// The chain pool while it is being interned: a record names a run of actions,
// and one run, wherever it is owed, is one entry.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/shared/gen/ChainIndex.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// A run of actions, laid end to end in one pool. A record carries the chain
// its production and its call sites owe it, never a single action: a rule
// that lays no record of its own hands its actions to the record below.
struct Chain {
  std::uint16_t off = 0, count = 0;
};

// What construction concludes about a chain and no run ever asks: whether it
// hands a node back, whether it opens one, and the write a caret reads it by.
struct ChainBuild {
  bool makes = false;
  bool creates = false;
  // The first action that writes a member. An optional mints a chain per
  // branch, so one member is spelled by several chains; they lead with the
  // same write, which is all a caret asks of any of them.
  std::uint16_t lead = kNoRow;
  // A nest turn's twin of the same run: its record reaches back over its
  // receiver, which the tree builder must tell from one that does not.
  bool turn = false;
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
  // The pools grow to meet the write, so what is bounded here is the id, not
  // the room. A flag, not a refusal of its own: the build reads it once and
  // names the cap, since a count that wrapped hands two chains one id.
  bool capped = false;

  // A chain is what it says, not where it stands: the same run of actions,
  // wherever it is owed, is one entry.
  std::uint16_t intern_chain(const std::uint16_t* a, std::size_t n) {
    return n == 0 ? kNoRow : intern(a, n);
  }

  // A record that says nothing, and is still a record: a call site's
  // directive needs a span to ride, and a production whose body opens none
  // gives it one.
  std::uint16_t bare_chain() { return intern(nullptr, 0); }

  // The two runs standing over one position, interned as one. A joined run is
  // built only where both hold something, which is the rare case.
  std::uint16_t intern_cat(const std::vector<std::uint16_t>& a,
                           const std::vector<std::uint16_t>& b) {
    if (b.empty()) return intern_chain(a.data(), a.size());
    if (a.empty()) return intern_chain(b.data(), b.size());
    return intern_joined(a, b);
  }

  // The same run as a turn's own chain.
  std::uint16_t turn_chain(std::uint16_t c) {
    if (c == kNoRow) return kNoRow;
    const Chain x = chains[c];
    const std::vector<std::uint16_t> run(acts.data() + x.off,
                                         acts.data() + x.off + x.count);
    return intern(run.data(), run.size(), true);
  }

  // A chain opens a record when it starts with a creation, and states nothing
  // else when that is all it is -- the second is what may travel up to the
  // chain that closes over it.
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

  // One chain after another, as one: what a production hands to the record
  // below it comes first, what the call site adds comes after.
  std::uint16_t concat(std::uint16_t a, std::uint16_t b) {
    if (a == kNoRow) return b;
    if (b == kNoRow) return a;
    const Chain x = chains[a], y = chains[b];
    const std::uint16_t* const ac = acts.data();
    return intern_joined({ac + x.off, x.count}, {ac + y.off, y.count});
  }

 private:
  ChainIndex _index{};

  // The run alone: `turn` is read at the probe, after a build may have
  // rewritten it.
  static std::uint64_t hash_run(const std::uint16_t* a, std::size_t n) {
    std::uint64_t h = hash_mix(0x9e3779b97f4a7c15ull, n);
    for (std::size_t k = 0; k < n; ++k) h = hash_mix(h, a[k]);
    return h;
  }

  std::uint16_t intern_joined(std::span<const std::uint16_t> a,
                              std::span<const std::uint16_t> b) {
    std::vector<std::uint16_t> j(a.begin(), a.end());
    j.insert(j.end(), b.begin(), b.end());
    return intern(j.data(), j.size());
  }

  std::uint16_t intern(const std::uint16_t* a, std::size_t n,
                       bool turn = false) {
    if (_index.head.size() < (std::size_t{nchains} + 1) * 2) {
      _index.init(std::size_t{nchains} * 2 + 8);
      for (std::uint16_t i = 0; i < nchains; ++i)
        _index.add(hash_run(acts.data() + chains[i].off, chains[i].count), i);
    }
    const std::uint64_t h = hash_run(a, n);
    const Chain* const ch = chains.data();
    const std::uint16_t* const ac = acts.data();
    for (unsigned i = _index.first(h); i != ChainIndex::kNone;
         i = _index.after(i))
      if (ch[i].count == n && chainBuild[i].turn == turn &&
          std::equal(a, a + n, ac + ch[i].off))
        return static_cast<std::uint16_t>(i);
    // A chain becomes an element id, which is sixteen bits: a count that
    // wrapped would hand two chains one id, and an AST written with the
    // wrong actions.
    if (nchains + 1u >= kNoElement || nacts + n > 0xFFFFu) {
      capped = true;
      return kNoRow;
    }
    const std::uint16_t i = nchains++;
    _index.add(h, i);
    chains[i] = Chain{nacts, static_cast<std::uint16_t>(n)};
    chainBuild[i] = {.turn = turn};  // the two pools are one length
    std::copy_n(a, n, acts.row(nacts, n));
    nacts = static_cast<std::uint16_t>(nacts + n);
    return i;
  }
};

}  // namespace cursiv
