// The coarsest refinement of a partition that labelled edges respect, by
// Hopcroft's splitting: a split moves and queues its smaller side, so no state
// moves more than log n times.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv::engine {

// `cls` gives each state its opening class; `each_edge(edge)` calls
// `edge(from, label, to)` for every edge, in a stable order, at most one per
// state and label. `reads` and `moves` count the work, where given.
template <class EachEdge>
std::vector<std::uint32_t> partition_refine(const std::vector<std::uint32_t>& cls,
                                            std::uint32_t nlabels,
                                            EachEdge&& each_edge,
                                            std::uint64_t* reads = nullptr,
                                            std::uint64_t* moves = nullptr) {
  const std::size_t n = cls.size();
  // `in[at[t]]` up to `in[at[t + 1]]` reach `t`.
  struct In {
    std::uint32_t from, label;
  };
  std::vector<std::uint32_t> at(n + 1, 0);
  each_edge([&](std::uint32_t, std::uint32_t, std::uint32_t to) { ++at[to + 1]; });
  for (std::size_t i = 1; i <= n; ++i) at[i] += at[i - 1];
  std::vector<In> in(at[n]);
  {
    std::vector<std::uint32_t> put(at.begin(), at.end() - 1);
    each_edge([&](std::uint32_t from, std::uint32_t label, std::uint32_t to) {
      in[put[to]++] = In{from, label};
    });
  }
  // Block `b` is `elem[first[b]]` up to `elem[last[b]]`; a pass moves the
  // states it marks to the front, up to `mid[b]`.
  std::uint32_t initial = 0;
  for (const std::uint32_t c : cls) initial = std::max(initial, c + 1);
  std::vector<std::uint32_t> blk(cls), elem(n), pos(n);
  std::vector<std::uint32_t> first(initial + 1, 0), last, mid;
  for (const std::uint32_t c : cls) ++first[c + 1];
  for (std::size_t b = 1; b < first.size(); ++b) first[b] += first[b - 1];
  last.assign(first.begin() + 1, first.end());
  first.pop_back();
  mid = first;
  for (std::size_t s = 0; s < n; ++s) {
    pos[s] = mid[blk[s]]++;
    elem[pos[s]] = static_cast<std::uint32_t>(s);
  }
  mid = first;
  std::vector<std::uint32_t> work(first.size()), touched, labels;
  for (std::uint32_t b = 0; b < work.size(); ++b) work[b] = b;
  std::vector<std::vector<std::uint32_t>> byLabel(nlabels);
  while (!work.empty()) {
    const std::uint32_t a = work.back();
    work.pop_back();
    for (auto e = first[a]; e < last[a]; ++e)
      for (auto i = at[elem[e]]; i < at[elem[e] + 1]; ++i) {
        if (byLabel[in[i].label].empty()) labels.push_back(in[i].label);
        byLabel[in[i].label].push_back(in[i].from);
        if (reads != nullptr) ++*reads;
      }
    for (const std::uint32_t label : labels) {
      touched.clear();
      for (const std::uint32_t p : byLabel[label]) {
        const std::uint32_t b = blk[p];
        if (mid[b] == first[b]) touched.push_back(b);
        const std::uint32_t q = elem[mid[b]];
        elem[pos[p]] = q;
        pos[q] = pos[p];
        elem[mid[b]] = p;
        pos[p] = mid[b]++;
      }
      byLabel[label].clear();
      for (const std::uint32_t b : touched) {
        if (mid[b] == last[b]) {
          mid[b] = first[b];
          continue;
        }
        // The smaller side goes, and is what the block is split by from here.
        const auto part = static_cast<std::uint32_t>(first.size());
        if (mid[b] - first[b] <= last[b] - mid[b]) {
          first.push_back(first[b]);
          last.push_back(mid[b]);
          first[b] = mid[b];
        } else {
          first.push_back(mid[b]);
          last.push_back(last[b]);
          last[b] = mid[b];
        }
        mid[b] = first[b];
        mid.push_back(first[part]);
        work.push_back(part);
        for (auto i = first[part]; i < last[part]; ++i) blk[elem[i]] = part;
        if (moves != nullptr) *moves += last[part] - first[part];
      }
    }
    labels.clear();
  }
  return blk;
}

}  // namespace cursiv::engine
