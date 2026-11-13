// Each id of a list once, where it first stands.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/shared/utils/RoundMarks.hpp"

namespace cursiv::engine {

// One mark per id a list can hold, so a list costs its own length however many
// ids it repeats.
class KeepFirst {
 public:
  // `reads`, if any, counts the ids read.
  explicit KeepFirst(std::uint64_t* reads = nullptr) : _reads(reads) {}

  // Moves the ids to keep to the front of `ids`, and returns how many there
  // are.
  std::size_t operator()(std::span<std::uint16_t> ids) {
    _seen.next();
    if (_reads != nullptr) *_reads += ids.size();
    std::size_t kept = 0;
    for (const std::uint16_t id : ids) {
      if (_seen.first(id)) ids[kept++] = id;
    }
    return kept;
  }

 private:
  RoundMarks _seen;
  std::uint64_t* _reads;
};

}  // namespace cursiv::engine
