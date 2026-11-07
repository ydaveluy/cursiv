// Computes semantic tokens from the grammar and the name provider. A server
// announces semantic tokens only for a language that installs a provider, so a
// language asks for this one in its `install_lsp_module`; an editor then paints
// its colours over its own syntax rules. Not templated by the grammar:
// everything read here is already erased except the name provider's typed
// lookup.
#pragma once

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/semantic/AbstractSemanticTokenProvider.hpp"

namespace cursiv {

// Colours with the legend's `keyword`, `type`, `variable`, `property`,
// `number`, `string`, `comment` and `operator` types, and no modifier.
class GrammarSemanticTokenProvider : public AbstractSemanticTokenProvider {
 public:
  explicit GrammarSemanticTokenProvider(const CoreServices& core) noexcept
      : _core(core) {}

 protected:
  void computeHighlighting(const Document& document, TextOffset begin,
                           TextOffset end, SemanticTokenAcceptor accept,
                           const CancellationToken& token) const override;

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
