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
  // A refusal of a choice below the one the episode was asked: it holds while
  // the descent goes the way of the readings that refused it.
  bool below = false;
};

// A choice on the way to the refusals below that the generated automaton
// answers: the arms the readings took there, one bit each.
struct LlPlanWay {
  TextOffset at = 0;
  std::uint32_t lull = 0;
  std::uint16_t decision = kNoRow;
  std::uint64_t arms = 0;
};

class LlPlan {
 public:
  // Names a plan as one `install` laid it.
  using Laid = std::uint64_t;

  void clear() {
    _entries.clear();
    _ways.clear();
    _below = false;
    _repairs = 0;
    ++_laid;
  }
  bool empty() const { return _entries.empty(); }

  // The entries are in the order the descent meets them, so `at` never
  // decreases.
  const std::vector<LlPlanEntry>& entries() const { return _entries; }

  // Lays `entries` for the descent after `repairs` repairs, and `ways`, where
  // given, as the ways its refusals below hold on, in the order of `at` as the
  // entries; both get what the plan held before.
  Laid install(std::vector<LlPlanEntry>& entries, std::vector<LlPlanWay>* ways,
               std::uint32_t repairs) {
    _entries.swap(entries);
    _ways.clear();
    _below = ways != nullptr;
    if (_below) _ways.swap(*ways);
    _repairs = repairs;
    return ++_laid;
  }

  // Puts `path` after the plan `laid` names; false once it is no longer the
  // one laid.
  bool extend(Laid laid, const std::vector<LlPlanEntry>& path) {
    if (laid != _laid) return false;
    _entries.insert(_entries.end(), path.begin(), path.end());
    return true;
  }

  // The descent took `arm` at `decision` without asking the plan: the
  // refusals below stand while that is an arm the readings took there.
  void went(std::uint16_t decision, TextOffset at, std::uint32_t lull,
            std::uint16_t arm) {
    if (!_below) return;
    auto it = std::lower_bound(
        _ways.begin(), _ways.end(), at,
        [](const LlPlanWay& w, TextOffset p) { return w.at < p; });
    for (; it != _ways.end() && it->at == at; ++it)
      if (it->decision == decision && it->lull == lull) {
        if (arm < 64 && ((it->arms >> arm) & 1u) != 0) return;
        break;
      }
    drop_below();
  }

  // ...and once it may have gone another way, they are not the plan's.
  void drop_below() {
    if (!_below) return;
    std::erase_if(_entries, [](const LlPlanEntry& e) { return e.below; });
    _ways.clear();
    _below = false;
  }

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
  std::vector<LlPlanWay> _ways;
  bool _below = false;
  std::uint32_t _repairs = 0;
  Laid _laid = 0;
};

}  // namespace cursiv
