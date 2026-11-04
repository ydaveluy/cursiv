// One packed id space for the two kinds of symbol: terminals below the
// base, nonterminals above. Terminal 0 is end of input, nonterminal 0 the
// augmented start, and a right-hand side holds either without a tag to say
// which.
#pragma once

#include <cstdint>

namespace cursiv {

inline constexpr std::uint16_t kNtBase = 0x8000;

constexpr bool is_term(std::uint16_t s) { return s < kNtBase; }
constexpr std::uint16_t nt_index(std::uint16_t s) {
  return static_cast<std::uint16_t>(s - kNtBase);
}
constexpr std::uint16_t nt_sym(std::uint16_t i) {
  return static_cast<std::uint16_t>(kNtBase + i);
}

}  // namespace cursiv
