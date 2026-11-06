#pragma once

#include <string>

#include "cursiv/syntax-tree/ReferenceInfo.hpp"

// Why a reference did not resolve: the sentence, the site it was asked at, and
// the description that was found and refused, when there was one. The linker
// writes it, in `Linker::createLinkingError`.

namespace cursiv {

struct AstNodeDescription;

struct LinkingError {
  std::string message;
  ReferenceInfo info;
  // Set when a name was found and rejected -- by its type, say. Absent when
  // nothing carried the name at all.
  const AstNodeDescription* targetDescription = nullptr;
};

}  // namespace cursiv
