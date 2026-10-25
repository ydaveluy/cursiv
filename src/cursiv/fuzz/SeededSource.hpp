// The default ribbon: a seed. Reproducible by construction -- a seed and a
// document number replay that exact document.
#pragma once

#include <cstdint>

#include "cursiv/fuzz/DecisionSource.hpp"

namespace cursiv::fuzzer {

struct SeededSource final : DecisionSource {
  explicit SeededSource(std::uint64_t seed) : _s(seed | 1u) {}

  unsigned choose(DecisionKind, unsigned n) override {
    return n <= 1 ? 0u : static_cast<unsigned>(next() % n);
  }

  std::uint64_t next() {
    _s ^= _s << 13;
    _s ^= _s >> 7;
    _s ^= _s << 17;
    return _s;
  }

 private:
  std::uint64_t _s;
};

}  // namespace cursiv::fuzzer
