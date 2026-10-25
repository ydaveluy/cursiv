#pragma once

#include <lsp/types.h>

#include <span>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/workspace/Document.hpp"

// The caret a completion is asked at, as the hooks see it: the token being
// written -- `tokenOffset`..`tokenEndOffset`, both `offset` when none is --
// what may stand there, and the caret itself.

namespace cursiv {

struct CompletionContext {
  const Document& document;
  std::span<const NextFeature> features;
  TextOffset tokenOffset;
  TextOffset tokenEndOffset;
  TextOffset offset;
  lsp::Position position;
};

}  // namespace cursiv
