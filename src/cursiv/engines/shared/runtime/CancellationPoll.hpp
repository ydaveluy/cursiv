// A stop read where a parse can afford it: a cold loop counts its turns down
// and reads the token once per run of them; a parse with no token never does.
#pragma once

#include <cstdint>
#include <limits>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/macros.hpp"

namespace cursiv {

class CancellationPoll {
 public:
  CancellationPoll() = default;
  explicit CancellationPoll(const CancellationToken& token)
      : _token(token.stop_possible() ? &token : nullptr) {
    refill();
  }

  void poll() const {
    if (--_left == 0) [[unlikely]] check();
  }

  CURSIV_NOINLINE void check() const {
    refill();
    if (_token != nullptr) throw_if_cancelled(*_token);
  }

 private:
  static constexpr std::uint32_t kTurns = 256;

  void refill() const {
    _left = _token != nullptr ? kTurns : std::numeric_limits<std::uint32_t>::max();
  }

  const CancellationToken* _token = nullptr;
  mutable std::uint32_t _left = std::numeric_limits<std::uint32_t>::max();
};

}  // namespace cursiv
