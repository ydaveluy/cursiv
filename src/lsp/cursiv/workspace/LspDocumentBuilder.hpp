// The language server's builder: a rebuilt document is validated with its
// language's configured categories. A build given explicit options keeps them.
#pragma once

#include <span>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

struct SharedLspServices;

class LspDocumentBuilder : public DefaultDocumentBuilder {
 public:
  explicit LspDocumentBuilder(SharedLspServices& shared);

 protected:
  // The client's `validation` setting of the language, when it has one.
  [[nodiscard]] BuildOptions updateBuildOptionsFor(
      const CoreServices& language) const override;

 private:
  SharedLspServices& _lsp;
};

}  // namespace cursiv
