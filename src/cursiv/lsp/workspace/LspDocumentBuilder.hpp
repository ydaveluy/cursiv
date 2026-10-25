// The builder of a language server: a document an update rebuilds -- edited or
// relinked by someone else's edit -- is validated with its own language's
// configured categories, resolved when the build asks. A build handed explicit
// options keeps them.
#pragma once

#include <span>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/core/workspace/Document.hpp"

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
