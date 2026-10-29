// A mode needs shared suffixes only when a token can accept a prefix, then
// keep reading around a cycle without another guaranteed acceptance. A mode
// that reads a refusing terminal answers for its token's head, and never
// shares them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/utils/StrongComponents.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <class A>
inline std::vector<std::uint8_t> lex_backtrack_modes(
    const A& a, std::vector<std::uint8_t>* cycles = nullptr) {
  if (cycles != nullptr) cycles->clear();
  const std::uint32_t count = a.ndfa;
  if (count == 0 || a.nmodes == 0) return {};
  std::vector<std::uint8_t> guaranteed(count, 0), usable(count, 1);
  bool accepting = false;
  for (std::uint32_t s = 0; s < count; ++s) {
    if constexpr (requires { a.dfaRelay; })
      if (s < a.dfaRelay.size() && a.dfaRelay[s] != kNoRelay) usable[s] = 0;
    if (!usable[s] || a.dfaAccept[s] == 0) continue;
    accepting = true;
    guaranteed[s] = a.dfaBound[s] == kNoSet;
    if constexpr (requires { a.lexChecks; })
      if (s < a.lexChecks.size() && a.lexChecks[s] != 0) guaranteed[s] = 0;
  }
  if (!accepting) return {};
  const auto next = [&](std::uint32_t s, unsigned c) {
    if constexpr (requires { a.next_dfa(s, c); })
      return a.next_dfa(static_cast<std::uint16_t>(s),
                        static_cast<std::uint8_t>(c));
    else
      return a.dfaNext[std::size_t{s} * a.ncls + c];
  };
  // Keep both graphs in compressed rows. Only transitions whose two ends
  // can fail to accept belong to the graph in which cycles matter. A relay
  // captures the token head, so paths through that state cannot be shared.
  std::vector<std::uint32_t> starts(count + 1), backStarts(count + 1);
  for (std::uint32_t s = 0; s < count; ++s) {
    if (!usable[s]) continue;
    for (unsigned c = 0; c < a.ncls; ++c) {
      const auto to = next(s, c);
      if (to == kDeadState || to >= count || !usable[to]) continue;
      ++backStarts[to + 1];
      if (!guaranteed[s] && !guaranteed[to]) ++starts[s + 1];
    }
  }
  for (std::uint32_t s = 0; s < count; ++s) {
    starts[s + 1] += starts[s];
    backStarts[s + 1] += backStarts[s];
  }
  std::vector<std::uint32_t> edges(starts.back()), back(backStarts.back());
  auto fill = starts, backFill = backStarts;
  for (std::uint32_t s = 0; s < count; ++s) {
    if (!usable[s]) continue;
    for (unsigned c = 0; c < a.ncls; ++c) {
      const auto to = next(s, c);
      if (to == kDeadState || to >= count || !usable[to]) continue;
      back[backFill[to]++] = s;
      if (!guaranteed[s] && !guaranteed[to]) edges[fill[s]++] = to;
    }
  }
  const auto components = StrongComponents<>::build(starts, edges);
  std::vector<std::uint8_t> reaches(count, 0);
  std::vector<std::uint32_t> todo;
  const auto spread = [&] {
    for (std::size_t i = 0; i < todo.size(); ++i)
      for (auto k = backStarts[todo[i]]; k < backStarts[todo[i] + 1]; ++k)
        if (!reaches[back[k]]) {
          reaches[back[k]] = 1;
          todo.push_back(back[k]);
        }
  };
  for (std::uint32_t s = 0; s < count; ++s)
    if (components.cyclic[s]) reaches[s] = 1, todo.push_back(s);
  spread();
  // An acceptance before such a cycle is the prefix that may be retained.
  // Other guaranteed acceptances on that path supply a later prefix instead.
  todo.clear();
  for (std::uint32_t s = 0; s < count; ++s) {
    reaches[s] = reaches[s] && a.dfaAccept[s] != 0;
    if (reaches[s]) todo.push_back(s);
  }
  if (todo.empty()) return {};
  spread();
  std::vector<std::uint8_t> modes(a.nmodes, 0);
  bool any = false;
  for (unsigned m = 0; m < a.nmodes; ++m) {
    if constexpr (requires { a.modeHeads; })
      if (m < a.modeHeads.size() && a.modeHeads[m].any()) continue;
    const auto start = a.modeStart[m];
    if (start < count && reaches[start]) modes[m] = 1, any = true;
  }
  if (!any) return {};
  if (cycles != nullptr)
    cycles->assign(components.cyclic.begin(), components.cyclic.end());
  return modes;
}

}  // namespace cursiv
