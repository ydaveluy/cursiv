#pragma once

#include "engines/lr/IndentGrammar.hpp"

namespace tllindent {
struct Ll : tindent::G {};
struct Folded : tindent::folded::Cases {};
struct Bare : tindent::repaired::G {};
struct Laid : tindent::laid::Cases {};
}  // namespace tllindent
