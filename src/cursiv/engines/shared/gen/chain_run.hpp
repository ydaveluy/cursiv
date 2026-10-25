// The actions one chain of the write pool names, in order.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

template <class Built>
std::vector<std::uint16_t> chain_run(const Built& a, std::uint16_t i) {
  std::vector<std::uint16_t> r;
  const auto& c = a.writes.chains[i];
  for (std::uint32_t k = 0; k < c.count; ++k)
    r.push_back(a.writes.acts[c.off + k]);
  return r;
}

}  // namespace cursiv
