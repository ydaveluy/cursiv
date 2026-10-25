#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"

#include <lsp/types.h>

#include <string>

namespace cursiv {

// Row order is the wire format: reordering renumbers every token already
// sent. `tokenModifiers` stays empty -- `declaration`, `readonly` and the
// rest are facts this storey has no service to decide.
const lsp::SemanticTokensLegend& semantic_tokens_legend() {
  static const lsp::SemanticTokensLegend legend = [] {
    lsp::SemanticTokensLegend built;
    built.tokenTypes = {std::string("keyword"),  std::string("type"),
                        std::string("variable"), std::string("property"),
                        std::string("number"),   std::string("string"),
                        std::string("comment"),  std::string("operator")};
    return built;
  }();
  return legend;
}

}  // namespace cursiv
