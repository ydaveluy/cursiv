// Each id of a list once, where it first stands.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cursiv {

// One stamp per id a list can hold, so a list costs its own length however many
// ids it repeats.
class KeepFirst {
 public:
  // `reads`, if any, counts the ids read.
  explicit KeepFirst(std::uint64_t* reads = nullptr) : _reads(reads) {}

  // Moves the ids to keep to the front of `ids`, and says how many there are.
  std::size_t operator()(std::span<std::uint16_t> ids) {
    if (_stamp.empty()) _stamp.assign(std::size_t{1} << 16, 0);
    ++_round;
    if (_reads != nullptr) *_reads += ids.size();
    std::size_t kept = 0;
    for (const std::uint16_t id : ids) {
      if (_stamp[id] == _round) continue;
      _stamp[id] = _round;
      ids[kept++] = id;
    }
    return kept;
  }

 private:
  std::vector<std::uint32_t> _stamp;
  std::uint32_t _round = 0;
  std::uint64_t* _reads;
};

}  // namespace cursiv
