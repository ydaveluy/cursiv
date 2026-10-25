// The plans of the episodes of one parse, kept by what each read: the stack it
// stood under, and the text it looked at. A place that reads the same again is
// answered by the plan the first one made.
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlPlan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What an episode stood on and read. The token in hand is not the text alone:
// the alphabet that read it says which terminal it is and where it ends.
struct LlRemembered {
  std::uint32_t context = 0;
  TextOffset from = 0;
  TextOffset length = 0;
  TextOffset first = 0;
  TextOffset gap = 0;
  std::uint16_t term = kInvalidSymbol;
  std::uint16_t arm = kNoRow;
  std::uint8_t section = 0;
  // An episode that reached the end of the text depends on that end remaining
  // where it was.
  bool ended = false;
  std::uint32_t next = ~std::uint32_t{0};
  std::uint32_t plan = 0;
  std::uint32_t count = 0;
};

// The index only narrows the candidates: a hit still compares the stack, the
// text looked at and the dependency on the end of it.
inline std::uint64_t ll_remembered_hash(std::uint32_t context,
                                        std::string_view prefix,
                                        TextOffset first, TextOffset gap,
                                        std::uint16_t term,
                                        std::uint8_t section) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto add = [&](std::uint64_t value) {
    hash = (hash ^ value) * 1099511628211ull;
  };
  add(term);
  add(first);
  add(gap);
  add(section);
  add(context);
  const char* at = prefix.data();
  std::size_t left = prefix.size();
  for (; left >= 8; left -= 8, at += 8) {
    std::uint64_t word;
    std::memcpy(&word, at, 8);
    add(word);
  }
  for (; left != 0; --left, ++at) add(static_cast<unsigned char>(*at));
  return hash;
}

class LlEpisodeMemo {
 public:
  void clear() {
    _rows.clear();
    _head.clear();
    _plans.clear();
    _ended = false;
  }

  // An end-dependent answer only applies at the same remaining length.
  // Both chains are newest first; the later row remains the preferred plan.
  template <class Agrees>
  const LlRemembered* find(std::uint64_t hash, TextOffset room,
                           Agrees&& agrees) const {
    const auto* plain = find_at(hash, agrees);
    if (!_ended) return plain;
    const auto* ended = find_at(end_hash(hash, room), agrees);
    return plain == nullptr || (ended != nullptr && ended > plain) ? ended
                                                                   : plain;
  }

  // The plan is kept as distances from `row.from`.
  void add(std::uint64_t hash, LlRemembered row,
           const std::vector<LlPlanEntry>& plan) {
    if (row.ended) {
      hash = end_hash(hash, row.length);
      _ended = true;
    }
    auto& head = _head.try_emplace(hash, ~std::uint32_t{0}).first->second;
    row.next = head;
    row.plan = static_cast<std::uint32_t>(_plans.size());
    row.count = static_cast<std::uint32_t>(plan.size());
    head = static_cast<std::uint32_t>(_rows.size());
    _rows.push_back(row);
    for (LlPlanEntry e : plan) {
      e.at -= row.from;
      _plans.push_back(e);
    }
  }

  // The entries of a row, laid at `from`.
  void lay(const LlRemembered& row, TextOffset from,
           std::vector<LlPlanEntry>& out) const {
    out.clear();
    for (std::uint32_t i = 0; i < row.count; ++i) {
      LlPlanEntry e = _plans[row.plan + i];
      e.at += from;
      out.push_back(e);
    }
  }

 private:
  static std::uint64_t end_hash(std::uint64_t hash, TextOffset length) {
    return (hash ^ length) * 1099511628211ull;
  }
  template <class Agrees>
  const LlRemembered* find_at(std::uint64_t hash, Agrees&& agrees) const {
    const auto found = _head.find(hash);
    if (found == _head.end()) return nullptr;
    for (auto at = found->second; at != ~std::uint32_t{0}; at = _rows[at].next)
      if (agrees(_rows[at])) return &_rows[at];
    return nullptr;
  }

  std::vector<LlRemembered> _rows;
  std::unordered_map<std::uint64_t, std::uint32_t> _head;
  std::vector<LlPlanEntry> _plans;
  bool _ended = false;
};

}  // namespace cursiv
