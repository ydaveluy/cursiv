// What a lowering reads of a grammar besides its image: the symbols in the
// order they are numbered and the actions it publishes. All of it constant,
// made of the grammar's types.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/grammar/GrammarImage.hpp"

namespace cursiv {

struct AstRowDesc;

struct LowerDesc {
  std::span<const detail::ImageRuleDesc* const> terms;
  std::span<const detail::ImageRuleDesc* const> predicateTerms;
  std::span<const detail::ImageRuleDesc* const> nts;
  std::span<const AstRowDesc* const> rows;
  const detail::ImageRuleDesc* entry = nullptr;
  detail::ImageClassDesc bound{};
  const detail::ImageDesc* indentOpen = nullptr;
  const detail::ImageDesc* indentClose = nullptr;
  const detail::ImageRuleDesc* indentNewline = nullptr;
  std::uint32_t optionalGate = 0;
  // The creation the entry's own record may owe.
  const void* entryCreate = nullptr;
};

}  // namespace cursiv
