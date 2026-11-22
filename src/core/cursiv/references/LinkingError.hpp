#pragma once

#include <string>

#include "cursiv/syntax-tree/ReferenceInfo.hpp"

// Why a reference did not resolve: the sentence and the site it was asked at.
// The linker writes it, in `Linker::createLinkingError`.

namespace cursiv {

struct AstNodeDescription;

struct LinkingError {
  std::string message;
  ReferenceInfo info;
  // Set only when the caller of `createLinkingError` passes one: a linking
  // diagnostic carries none.
  const AstNodeDescription* targetDescription = nullptr;
};

}  // namespace cursiv
