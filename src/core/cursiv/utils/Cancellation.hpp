#pragma once

#include <stdexcept>
#include <stop_token>

// Cooperative cancellation, as the standard already spells it: a long build
// polls the token it was handed and gives up where it stands.

namespace cursiv {

using CancellationToken = std::stop_token;
using CancellationTokenSource = std::stop_source;

// What an API that accepts cancellation gets when the caller has none.
inline const CancellationToken kNoCancel{};

class OperationCancelled : public std::runtime_error {
 public:
  OperationCancelled();
};

inline void throw_if_cancelled(const CancellationToken& token) {
  if (token.stop_requested()) [[unlikely]]
    throw OperationCancelled();
}

}  // namespace cursiv
