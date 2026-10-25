// The subsets of a lexer's DFA, as bit rows in one arena. Held as a list, a
// subset costs an allocation, a sort and an element-wise compare per
// transition; held as bits it costs the words it spans, its canonical order
// the bit order, so nothing is sorted.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/ChainIndex.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"

namespace cursiv {

struct LexNfaSubsets {
  std::size_t words = 0;
  // The rows, packed to the span each one occupies: a subset of an NFA of
  // thousands of states touches a handful of words, and hashing or comparing
  // the whole width would put the cost of the widest set on every transition.
  std::vector<std::uint64_t> pool;
  std::vector<std::uint32_t> off, lo, hi;
  std::vector<std::uint64_t> cur;  // the one being built
  std::vector<int> touched;        // the states set in `cur`
  std::size_t tn = 0;      // ...and how many of them, the room being fixed
  std::vector<int> stack;  // the epsilon walk, explicit
  std::size_t curLo = 0, curHi = 0;
  // Sized for the subsets, which number with the NFA's states.
  ChainIndex ix;

  void init(std::size_t nstates) {
    // An empty subset keeps its span empty (lo > hi) even where the NFA has
    // no state at all.
    words = std::max<std::size_t>(1, (nstates + 63) / 64);
    pool.clear();
    off.clear();
    lo.clear();
    hi.clear();
    cur.assign(words, 0);
    // Room for the deepest walk and the widest mark list. THE PUSH IS NOT
    // GUARDED -- the mark is tested at the pop -- so a state is pushed once per
    // edge that reaches it; what bounds the stack is that it EXPANDS at most
    // once, and an expansion pushes at most two.
    stack.assign(nstates * 2 + 4, 0);
    touched.assign(nstates + 1, 0);
    tn = 0;
    curLo = words;
    curHi = 0;
    ix.init(nstates);
  }
  std::size_t count() const { return off.size(); }

  void close(const LexNfaPool& n, int s) {
    if (s < 0) return;
    int* const st = stack.data();
    const LexNfaState* const ns = n.s.data();
    std::uint64_t* const cw = cur.data();
    int* const tw = touched.data();
    std::size_t sp = 0;
    st[sp++] = s;
    while (sp != 0) {
      const int x = st[--sp];
      const std::size_t k = static_cast<std::size_t>(x) >> 6;
      const std::uint64_t b = std::uint64_t{1} << (x & 63);
      if (cw[k] & b) continue;
      cw[k] |= b;
      tw[tn++] = x;
      curLo = std::min(k, curLo);
      curHi = std::max(k, curHi);
      const LexNfaState& e = ns[static_cast<std::size_t>(x)];
      if (e.set >= 0) continue;
      if (e.t1 >= 0) st[sp++] = e.t1;
      if (e.t2 >= 0) st[sp++] = e.t2;
    }
  }

  // Interns the row being built and clears it: every bit it holds is in
  // `touched`, so zeroing those words empties it whole. Equal content gives
  // the same span, so the span is part of the identity and not a hint.
  unsigned intern() {
    std::uint64_t* const cw = cur.data();
    const std::uint32_t* const lop = lo.data();
    const std::uint32_t* const hip = hi.data();
    const std::uint32_t* const offp = off.data();
    const std::uint64_t* const pp = pool.data();
    std::uint64_t h = hash_mix(0x9e3779b97f4a7c15ull, curLo);
    h = hash_mix(h, curHi);
    for (std::size_t k = curLo; k <= curHi; ++k) h = hash_mix(h, cw[k]);
    unsigned found = ChainIndex::kNone;
    for (unsigned i = ix.first(h); i != ChainIndex::kNone; i = ix.after(i)) {
      if (lop[i] != curLo || hip[i] != curHi) continue;
      bool same = true;
      const std::size_t b = offp[i];
      for (std::size_t k = curLo; k <= curHi && same; ++k)
        same = pp[b + (k - curLo)] == cw[k];
      if (same) {
        found = i;
        break;
      }
    }
    if (found == ChainIndex::kNone) {
      found = static_cast<unsigned>(off.size());
      off.push_back(static_cast<std::uint32_t>(pool.size()));
      lo.push_back(static_cast<std::uint32_t>(curLo));
      hi.push_back(static_cast<std::uint32_t>(curHi));
      for (std::size_t k = curLo; k <= curHi; ++k) pool.push_back(cw[k]);
      ix.add(h, found);
    }
    const int* const tw = touched.data();
    for (std::size_t q = 0; q < tn; ++q)
      cw[static_cast<std::size_t>(tw[q]) >> 6] = 0;
    tn = 0;
    curLo = words;
    curHi = 0;
    return found;
  }
};

}  // namespace cursiv
