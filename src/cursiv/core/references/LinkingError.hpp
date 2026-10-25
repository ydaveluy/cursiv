#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"

// Why a reference did not resolve: the sentence, the site it was asked at, and
// the description that was found and refused, when there was one.

namespace cursiv {

struct AstReflection;
struct AstNodeDescription;

struct LinkingError {
  std::string message;
  ReferenceInfo info;
  // Set when a name was found and rejected -- by its type, say. Absent when
  // nothing carried the name at all.
  const AstNodeDescription* targetDescription = nullptr;
};

// The sentences, and the one place they are written. Both doors word a
// failure through these: the linker's own error, and the diagnostic a
// validator publishes. It names the expected type -- the site's narrowing when
// it has one, the member otherwise -- and the name read, so the reader learns
// what the site wanted.
[[nodiscard]] std::string unresolved_reference_message(
    const AstReflection& reflection, std::uint16_t reference,
    std::uint8_t narrow, std::string_view text);

}  // namespace cursiv
