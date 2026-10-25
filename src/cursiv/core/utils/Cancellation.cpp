#include "cursiv/core/utils/Cancellation.hpp"

#include <stdexcept>

namespace cursiv {

OperationCancelled::OperationCancelled()
    : std::runtime_error("operation cancelled") {}

}  // namespace cursiv
