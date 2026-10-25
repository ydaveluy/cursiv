// What a lowering reads of a grammar besides its image: the symbols in the
// order they are numbered and the actions it publishes. All of it constant,
// made of the grammar's types.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/core/grammar/GrammarImage.hpp"

namespace cursiv {

struct AstRowDesc;

struct LowerDesc {
  std::span<const ImageRuleDesc* const> terms;
  std::span<const ImageRuleDesc* const> predicateTerms;
  std::span<const ImageRuleDesc* const> nts;
  std::span<const AstRowDesc* const> rows;
  const ImageRuleDesc* entry = nullptr;
  ImageClassDesc bound{};
  const ImageDesc* indentOpen = nullptr;
  const ImageDesc* indentClose = nullptr;
  const ImageRuleDesc* indentNewline = nullptr;
  std::uint32_t optionalGate = 0;
  // The creation the entry's own record may owe.
  const void* entryCreate = nullptr;
};

}  // namespace cursiv
