#include "cursiv/utils/Cancellation.hpp"

#include <stdexcept>

namespace cursiv {

OperationCancelled::OperationCancelled()
    : std::runtime_error("operation cancelled") {}

[[noreturn]] void OperationCancelled::raise() {
  throw OperationCancelled();
}

}  // namespace cursiv
