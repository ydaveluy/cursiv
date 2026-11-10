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
  void prepareBuild(std::span<Document* const>,
                    const BuildOptions&) const override;
  void buildDocuments(std::span<Document* const>,
                      const CancellationToken&) const override;

 private:
  // `updateBuildOptions` when the document's language has no configuration.
  [[nodiscard]] BuildOptions configuredBuildOptions(const Document&) const;

  SharedLspServices& _lsp;
};

}  // namespace cursiv
