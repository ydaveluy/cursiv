// A stop read where a parse can afford it: a cold loop counts its turns down
// and reads the token once per run of them; a parse with no token never does.
#pragma once

#include <cstdint>
#include <limits>
#include <utility>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

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

// The stop of the parse this thread runs, for generated code that no context
// reaches.
class ThreadStop {
 public:
  explicit ThreadStop(const CancellationToken& token)
      : _previous(std::exchange(current(), &token)) {}
  ~ThreadStop() { current() = _previous; }
  ThreadStop(const ThreadStop&) = delete;
  ThreadStop& operator=(const ThreadStop&) = delete;

  static CancellationPoll poll() {
    const CancellationToken* const token = current();
    return token != nullptr ? CancellationPoll(*token) : CancellationPoll();
  }

 private:
  static const CancellationToken*& current() {
    thread_local const CancellationToken* token = nullptr;
    return token;
  }

  const CancellationToken* _previous;
};

}  // namespace cursiv::engine
