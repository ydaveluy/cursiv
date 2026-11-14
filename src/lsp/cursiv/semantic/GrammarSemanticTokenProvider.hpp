// Semantic tokens from the grammar and the name provider. A language installs
// it in its `install_lsp_module`; an editor paints these colours over its
// syntax rules.
#pragma once

#include "cursiv/semantic/AbstractSemanticTokenProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// Colours with the legend's `keyword`, `type`, `variable`, `property`,
// `number`, `string`, `comment` and `operator` types, and no modifier.
class GrammarSemanticTokenProvider : public AbstractSemanticTokenProvider,
                                     protected DefaultCoreService {
 public:
  explicit GrammarSemanticTokenProvider(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

 protected:
  void computeHighlighting(const Document& document, TextOffset begin,
                           TextOffset end, SemanticTokenAcceptor accept,
                           const CancellationToken& token) const override;
};

}  // namespace cursiv
