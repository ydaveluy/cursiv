// How the operators of one level apply: a binary level groups a chain of them
// to the left, to the right or not at all; a unary level reads its operator
// before the operand (`prefix`) or after it (`postfix`).
#pragma once

#include <cstdint>

namespace cursiv {

enum class Association : std::uint8_t { Left, Right, Non, Prefix, Postfix };

}  // namespace cursiv
