// A transition the gap before its token decides: the conditions it asks, and
// the state each outcome lands in.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv {

struct LrGuard {
  static constexpr std::uint16_t kNoState = 0xffffu;
  // Bit `b` of an outcome is condition `conds[b]`; `kNoState` is an outcome
  // with no landing.
  std::vector<std::uint8_t> conds;
  std::vector<std::uint16_t> to;
  // Per outcome, the call site and the enclosing rule's an action carries.
  std::vector<ElementId> site;
  std::vector<ElementId> outer;

  bool operator==(const LrGuard&) const = default;

  // Each outcome's landing, in outcome order.
  template <class F>
  void each_landing(F&& f) const {
    for (const std::uint16_t t : to)
      if (t != kNoState) f(t);
  }
};

// A guard's program as a key, each landing as `name` gives it.
template <class F>
void lr_guard_key(const LrGuard& p, F&& name, std::vector<std::uint32_t>& key) {
  key.assign(p.conds.begin(), p.conds.end());
  key.push_back(0xffffffffu);
  for (const std::uint16_t to : p.to)
    key.push_back(to == LrGuard::kNoState ? 0xffffffffu : name(to));
  key.insert(key.end(), p.site.begin(), p.site.end());
  key.insert(key.end(), p.outer.begin(), p.outer.end());
}

}  // namespace cursiv
