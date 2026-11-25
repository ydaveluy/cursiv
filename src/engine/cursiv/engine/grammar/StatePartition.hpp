#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include "cursiv/engine/grammar/Refused.hpp"

namespace cursiv::engine::gen {

// Refine the initial blocks by their transitions. Each edge gives its source,
// destination and class. Returns the number of blocks after refinement.
std::uint32_t refine_partition(std::vector<std::uint32_t>& blockOf, std::uint32_t blocks,
                               unsigned classes, const auto& edges) {
  const auto n = blockOf.size();
  std::vector<std::uint32_t> first(n * classes + 1);
  std::size_t count = 0;
  edges([&](std::uint32_t, std::uint32_t to, unsigned c) {
    ++first[std::size_t{to} * classes + c];
    ++count;
  });
  if (count > std::numeric_limits<std::uint32_t>::max()) throw Refused("the automaton has too many transitions");
  std::partial_sum(first.begin(), first.end(), first.begin());
  std::vector<std::uint32_t> from(first.back());
  edges([&](std::uint32_t s, std::uint32_t to, unsigned c) {
    from[--first[std::size_t{to} * classes + c]] = s;
  });
  // Members of a block are contiguous. Marked members move to its front.
  std::vector<std::uint32_t> members(n), place(n), begin(blocks + 1);
  for (const auto b : blockOf) ++begin[b + 1];
  std::partial_sum(begin.begin(), begin.end(), begin.begin());
  begin.pop_back();
  auto end = begin;
  for (std::uint32_t s = 0; s < n; ++s) {
    place[s] = end[blockOf[s]]++;
    members[place[s]] = s;
  }
  std::vector<std::uint32_t> work(blocks), marked(blocks), touched, targets;
  std::iota(work.begin(), work.end(), 0u);
  while (!work.empty()) {
    const auto splitter = work.back();
    work.pop_back();
    targets.assign(members.begin() + begin[splitter], members.begin() + end[splitter]);
    for (unsigned c = 0; c < classes; ++c) {
      for (const auto t : targets) {
        const auto row = std::size_t{t} * classes + c;
        for (auto i = first[row]; i < first[row + 1]; ++i) {
          const auto s = from[i];
          const auto b = blockOf[s];
          const auto front = begin[b] + marked[b];
          if (place[s] < front) continue;
          if (marked[b]++ == 0) touched.push_back(b);
          const auto other = members[front];
          std::swap(members[front], members[place[s]]);
          std::swap(place[other], place[s]);
        }
      }
      for (const auto b : touched) {
        const auto middle = begin[b] + marked[b];
        marked[b] = 0;
        if (middle == end[b]) continue;
        // Only the smaller half is a new splitter. The larger retains the old block's pending work.
        const auto fresh = blocks++;
        if (middle - begin[b] <= end[b] - middle) {
          begin.push_back(begin[b]);
          end.push_back(middle);
          begin[b] = middle;
        } else {
          begin.push_back(middle);
          end.push_back(end[b]);
          end[b] = middle;
        }
        for (auto i = begin[fresh]; i < end[fresh]; ++i) blockOf[members[i]] = fresh;
        marked.push_back(0);
        work.push_back(fresh);
      }
      touched.clear();
    }
  }
  return blocks;
}

}  // namespace cursiv::engine::gen
