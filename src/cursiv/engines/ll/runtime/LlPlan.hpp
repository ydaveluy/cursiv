// The decisions an episode elected, in the order the descent will meet them:
// the descent asks for each where it stands, and is answered without reading
// the text again. A plan holds only while the descent goes where it went.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// "The plan does not say."
inline constexpr std::uint16_t kLlUnplanned = 0xFFFDu;

struct LlPlanEntry {
  TextOffset at = 0;
  // The tokens of no width read at `at` before the choice.
  std::uint32_t lull = 0;
  std::uint16_t decision = kNoRow;
  // `kNoRow` where the readings all died at this choice.
  std::uint16_t arm = kNoRow;
  bool taken = false;
};

class LlPlan {
 public:
  void clear() {
    _entries.clear();
    _repairs = 0;
  }
  bool empty() const { return _entries.empty(); }

  // The entries are in the order the descent meets them, so `at` never
  // decreases.
  std::vector<LlPlanEntry>& entries() { return _entries; }
  void hold(std::uint32_t repairs) { _repairs = repairs; }

  // What the plan says of `decision` where the descent stands at `at`, after
  // `repairs` repairs: nothing once the descent has been mended. A choice
  // the descent met is answered once -- a rule that may be empty meets one
  // choice at one place several times -- and a refusal as often as it asks.
  // An `again` choice is answered as often as it asks: a climb asks its
  // operator once per level it unwinds over one token.
  std::uint16_t say(std::uint16_t decision, TextOffset at, std::uint32_t lull,
                    std::uint32_t repairs, bool again) {
    if (_entries.empty() || repairs != _repairs) return kLlUnplanned;
    auto it = std::lower_bound(
        _entries.begin(), _entries.end(), at,
        [](const LlPlanEntry& e, TextOffset p) { return e.at < p; });
    for (; it != _entries.end() && it->at == at; ++it) {
      if (it->decision != decision || it->lull != lull || it->taken) continue;
      it->taken = !again && it->arm != kNoRow;
      return it->arm;
    }
    return kLlUnplanned;
  }

 private:
  std::vector<LlPlanEntry> _entries;
  std::uint32_t _repairs = 0;
};

}  // namespace cursiv
