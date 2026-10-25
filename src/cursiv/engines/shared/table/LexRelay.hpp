// A token the lexer reads only up to its echo: the run it marked, where the
// echo stands, and what the rest of the token is. The walk hands the rest
// to a search from the state it stops in, and resumes past the echo.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct PredicateCursor;
template <class Cursor>
class PredicateStates;
using PredicateState = PredicateStates<PredicateCursor>;

inline constexpr std::uint16_t kNoRelay = 0xffffu;

struct LexRelay {
  std::uint16_t term = 0;
  // The run's class, and the bytes of the literal between the run's end
  // and the point the walk stopped at.
  std::uint16_t markSet = 0;
  std::uint16_t fence = 0;
  // The echo stands in a closer: `before` and `after` are bytes of the
  // name pool framing the echoed run, and the token runs up to the first
  // such closer. Without a closer the echo stands where the walk stopped.
  bool closes = false;
  std::uint16_t beforeOff = 0, beforeLen = 0;
  std::uint16_t afterOff = 0, afterLen = 0;
  // The lexer state the walk resumes in past the echo.
  std::uint16_t resume = 0;
  // No echo: `before` is the opener and `after` the closer, and the search
  // ends at the closer that balances them. The walk may have read the first
  // content byte to keep a refusal out: the search starts that far behind.
  bool nested = false;
  std::uint8_t back = 0;
  std::uint16_t next = kNoRelay;
  // A closer's run of this class between `before` and the echo.
  std::uint16_t padSet = kNoSet;
  // Nested, and a side with several literals: each side is a list, every
  // literal behind a byte of its length.
  bool several = false;
  // Nested, and no closer to balance it: the run ends with the text.
  bool eof = false;
  // The tier of the branch this relay stands in: what it reads is a literal
  // or a keyword, behind conditions.
  std::uint8_t tier = 0;

  bool (*asserts)(const char*, TextOffset, TextOffset, TextOffset&) = nullptr;
  PredicateState (*recognizes)(const char*, TextOffset, TextOffset,
                               TextOffset&) = nullptr;

  constexpr bool operator==(const LexRelay&) const = default;
};

}  // namespace cursiv
