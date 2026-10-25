// A lowering's refusal told on the rule an author wrote: the sentence, its
// shape and the cycle it stands on.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/engines/lr/gen/LrLowered.hpp"

namespace cursiv {

std::uint16_t lr_fault_rule(const LrLowered& o);

std::string lr_fault_message(const LrLowered& o, std::string_view written);

}  // namespace cursiv
