#include "cursiv/test/ExpectedHover.hpp"

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/hover/HoverProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/ReplacedIndices.hpp"

namespace cursiv::test {

Disposable expectHover(const LspServices& services,
                       const ExpectedHover& expectedHover) {
  const ReplacedIndices marked =
      replaceIndices({.text = expectedHover.text,
                      .indexMarker = expectedHover.indexMarker,
                      .rangeStartMarker = expectedHover.rangeStartMarker,
                      .rangeEndMarker = expectedHover.rangeEndMarker});
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, expectedHover.parseOptions);

  std::optional<lsp::Hover> hover;
  if (services.hoverProvider != nullptr)
    hover = services.hoverProvider->getHoverContent(
        *document,
        to_lsp(document->textDocument().positionAt(
            marked.indices[expectedHover.index])),
        kNoCancel);

  // Only markup is prose: the deprecated `MarkedString` arms answer nothing.
  std::optional<std::string> content;
  if (hover.has_value())
    if (const auto* const markup =
            std::get_if<lsp::MarkupContent>(&hover->contents))
      content = markup->value;

  if (expectedHover.hoverMatches) {
    const std::string value = content.value_or(std::string{});
    expectedFunction(expectedHover.hoverMatches(value) ? "true" : "false",
                     "true", "hover '" + value + "' does not match");
  } else {
    expectedFunction(content.value_or("<none>"),
                     expectedHover.hover.value_or("<none>"));
  }

  Disposable disposable = documentDisposable(services, document);
  if (expectedHover.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
