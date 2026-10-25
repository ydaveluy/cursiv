// A token no lexer read, held by a reading: it stands whatever the mode until
// it is shifted, and behind an invented one waits the author's, `held`.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LrPin {
  Token pinTok{};
  // Conditions are asked of it while it stands.
  Token condTok{};
  Token held{};
  std::uint16_t heldMode = kNoMode;
  bool pin = false;
  bool pinRep = false;
};

}  // namespace cursiv
