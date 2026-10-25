// Failed DFA suffixes belong to their state and position. Acceptances and
// echoing relays remain the full matcher's responsibility; a relay that only
// asserts is a state read like another, its reach part of the halt.
#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <class Tape>
class LexFailedWalks {
 public:
  template <class Poll>
  bool fails(const Tape& tape, const char* src, TextOffset len,
             std::uint16_t mode, TextOffset from, TextOffset& halt,
             Poll&& poll) {
    std::uint16_t state = tape.start(mode);
    TextOffset at = from;
    TextOffset head = 0;
    TextOffset stop = 0;
    _stood.clear();
    for (;;) {
      if (tape.accept(state) != 0) return false;
      if (at % kStride == 0) {
        poll();
        const std::uint64_t key = (std::uint64_t{at} << 16) | state;
        if (const auto it = _known.find(key); it != _known.end()) {
          stop = it->second;
          break;
        }
        _stood.push_back(Stood{key, 0});
      }
      if constexpr (requires { tape.relay(state); })
        for (auto k = tape.relay(state); k != kNoRelay;
             k = tape.relay_of(k).next) {
          const LexRelay& relay = tape.relay_of(k);
          TextOffset seen = at;
          if (relay.asserts == nullptr || relay.asserts(src, len, at, seen))
            return false;
          TextOffset& reach = _stood.empty() ? head : _stood.back().reach;
          reach = std::max(reach, seen);
        }
      if (at >= len) {
        stop = len;
        break;
      }
      const auto next = tape.step(state, static_cast<unsigned char>(src[at]));
      if (next == kDeadState) {
        stop = at;
        break;
      }
      state = next;
      ++at;
    }
    // A key answers for the walk from itself on, not for what was read before.
    for (auto it = _stood.rbegin(); it != _stood.rend(); ++it) {
      stop = std::max(stop, it->reach);
      _known.emplace(it->key, stop);
    }
    halt = std::max(stop, head);
    return true;
  }

  bool fails(const Tape& tape, const char* src, TextOffset len,
             std::uint16_t mode, TextOffset from, TextOffset& halt) {
    return fails(tape, src, len, mode, from, halt, [] {});
  }

 private:
  static constexpr TextOffset kStride = 16;
  // A key stood on, and how far the assertions read from there up to the next
  // key.
  struct Stood {
    std::uint64_t key;
    TextOffset reach;
  };
  std::unordered_map<std::uint64_t, TextOffset> _known;
  std::vector<Stood> _stood;
};

}  // namespace cursiv
