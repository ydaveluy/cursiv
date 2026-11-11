// The walks of one text that match nothing: each failed suffix is read once.
#pragma once

#include <cstdint>

#include "cursiv/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

template <class Tape>
class LexFailedWalks {
 public:
  // Whether the walk of `mode` from `from` fails, `halt` where it stopped
  // reading. An acceptance or a relay it crosses leaves it to the full matcher.
  template <class Poll>
  bool fails(const Tape& tape, const char* src, TextOffset len,
             std::uint16_t mode, TextOffset from, TextOffset& halt,
             Poll&& poll) {
    const auto walk =
        _walks.match<true>(tape, src, len, tape.start(mode), from, from,
                           [&](TextOffset) { poll(); });
    if (walk.available) halt = walk.halt;
    return walk.available;
  }

 private:
  LexSuffixCache _walks;
};

}  // namespace cursiv::engine
