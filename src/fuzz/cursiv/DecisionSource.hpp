// The ribbon of decisions. Every choice the generation makes goes through
// this one door -- which alternative, how many rounds, which byte -- so a
// seed and a fuzzer's bytes are the same mechanism seen twice. The
// encoding lives in `DecisionCodec`, never here.
#pragma once

#include <cstdint>

namespace cursiv::fuzzer {

// Three kinds and no more. The set is the fuzzer's wire format, so it
// stays minimal and stable; a source that weighs them differently reads
// the tag, and every other one ignores it.
enum class DecisionKind : std::uint8_t {
  entry,  // which alternative of a choice to take
  style,  // how a terminal's value is rendered
  byte,   // which byte of a class, and how long a run is
};

struct DecisionSource {
  DecisionSource() = default;
  DecisionSource(const DecisionSource&) = delete;
  DecisionSource& operator=(const DecisionSource&) = delete;
  virtual ~DecisionSource() = default;

  // One decision among `n`; never called with `n == 0`.
  virtual unsigned choose(DecisionKind tag, unsigned n) = 0;

  // A ribbon that has run out. The generation then falls back on the shortest
  // derivation instead of inventing entropy of its own.
  [[nodiscard]] virtual bool exhausted() const { return false; }
};

}  // namespace cursiv::fuzzer
