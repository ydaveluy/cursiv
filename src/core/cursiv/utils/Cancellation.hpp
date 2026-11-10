#pragma once

#include <stdexcept>
#include <stop_token>

// Cooperative cancellation with `std::stop_token`: a long build polls its token
// and stops where it is.

namespace cursiv {

using CancellationToken = std::stop_token;
using CancellationTokenSource = std::stop_source;

// What an API that accepts cancellation gets when the caller has none.
inline const CancellationToken kNoCancel{};

class OperationCancelled : public std::runtime_error {
 public:
  OperationCancelled();

  [[noreturn]] static void raise();
};

inline void throw_if_cancelled(const CancellationToken& token) {
  if (token.stop_requested()) [[unlikely]]
    OperationCancelled::raise();
}

}  // namespace cursiv
