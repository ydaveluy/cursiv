// The decisions an episode elected, in the order the descent will reach them:
// the descent asks for each where it stands, and is answered without reading
// the text again. A plan holds only while the descent goes where it went.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

// "The plan does not say."
inline constexpr std::uint16_t kLlUnplanned = 0xFFFDu;

struct LlPlanEntry {
  TextOffset at = 0;
  // The tokens of no width read at `at` before the choice.
  std::uint32_t zeroWidth = 0;
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
  std::uint32_t zeroWidth = 0;
  std::uint16_t decision = kNoRow;
  std::uint64_t arms = 0;
};

class LlPlan {
 public:
  // Names one plan that `install` set.
  using Generation = std::uint64_t;

  void clear() {
    _entries.clear();
    _ways.clear();
    _below = false;
    _repairs = 0;
    ++_layout;
    ++_generation;
  }
  bool empty() const { return _entries.empty(); }

  // The entries are in the order the descent reaches them, so `at` never
  // decreases.
  const std::vector<LlPlanEntry>& entries() const { return _entries; }

  // Installs `entries` for the descent after `repairs` repairs, and `ways`,
  // where given, as the ways its refusals below hold on, in the order of `at`
  // as the entries; both get what the plan held before.
  Generation install(std::vector<LlPlanEntry>& entries,
                     std::vector<LlPlanWay>* ways, std::uint32_t repairs) {
    _entries.swap(entries);
    ++_layout;
    _ways.clear();
    _below = ways != nullptr;
    if (_below) _ways.swap(*ways);
    _repairs = repairs;
    return ++_generation;
  }

  // Appends `path` to the plan `generation` names; false once another plan has
  // replaced it.
  bool extend(Generation generation, const std::vector<LlPlanEntry>& path) {
    if (generation != _generation) return false;
    _entries.insert(_entries.end(), path.begin(), path.end());
    return true;
  }

  // The descent took `arm` at `decision` without asking the plan: the
  // refusals below stand while that is an arm the readings took there.
  void went(std::uint16_t decision, TextOffset at, std::uint32_t zeroWidth,
            std::uint16_t arm) {
    if (!_below) return;
    auto it = std::lower_bound(
        _ways.begin(), _ways.end(), at,
        [](const LlPlanWay& w, TextOffset p) { return w.at < p; });
    for (; it != _ways.end() && it->at == at; ++it)
      if (it->decision == decision && it->zeroWidth == zeroWidth) {
        if (arm < 64 && ((it->arms >> arm) & 1u) != 0) return;
        break;
      }
    drop_below();
  }

  // Once the descent may have gone another way, the refusals below are void.
  void drop_below() {
    if (!_below) return;
    std::erase_if(_entries, [](const LlPlanEntry& e) { return e.below; });
    ++_layout;
    _ways.clear();
    _below = false;
  }

  // What the plan says of `decision` at `at` after `repairs` repairs; nothing
  // once the descent was mended. A choice met is answered once, a refusal or an
  // `again` choice each time it asks.
  std::uint16_t say(std::uint16_t decision, TextOffset at,
                    std::uint32_t zeroWidth, std::uint32_t repairs,
                    bool again) {
    if (_entries.empty() || repairs != _repairs) return kLlUnplanned;
    // Many levels may ask one choice at one place: each picks up where the last
    // one left it, past the entries that cannot answer it.
    if (decision >= _cursors.size()) _cursors.resize(decision + 1u);
    Cursor& cursor = _cursors[decision];
    if (cursor.layout != _layout || cursor.at != at) cursor = {_layout, at, 0};
    // Past the entries before this place, among them those added since.
    if (cursor.next < _entries.size() && _entries[cursor.next].at < at) {
      const auto first = std::lower_bound(
          _entries.begin() + static_cast<std::ptrdiff_t>(cursor.next),
          _entries.end(), at,
          [](const LlPlanEntry& e, TextOffset p) { return e.at < p; });
      cursor.next = static_cast<std::size_t>(first - _entries.begin());
    }
    bool moving = true;
    for (std::size_t i = cursor.next;
         i < _entries.size() && _entries[i].at == at; ++i) {
      ++_visits;
      LlPlanEntry& e = _entries[i];
      if (e.decision != decision || e.taken) {
        if (moving) cursor.next = i + 1;
        continue;
      }
      moving = false;
      if (e.zeroWidth != zeroWidth) continue;
      e.taken = !again && e.arm != kNoRow;
      return e.arm;
    }
    return kLlUnplanned;
  }

  // The entries `say` has read, for a test of its growth.
  std::uint64_t visits() const { return _visits; }

 private:
  // Where a choice stands in the entries of the place it was last asked at.
  struct Cursor {
    std::uint64_t layout = 0;
    TextOffset at = 0;
    std::size_t next = 0;
  };

  std::vector<LlPlanEntry> _entries;
  std::vector<Cursor> _cursors;
  // Names the positions of `_entries`, which the entries count on.
  std::uint64_t _layout = 1;
  std::uint64_t _visits = 0;
  std::vector<LlPlanWay> _ways;
  bool _below = false;
  std::uint32_t _repairs = 0;
  Generation _generation = 0;
};

}  // namespace cursiv::engine
