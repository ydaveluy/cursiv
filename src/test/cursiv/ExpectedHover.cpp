#include "cursiv/ExpectedHover.hpp"

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/hover/HoverProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/ReplacedIndices.hpp"

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
  const auto* const caret =
      detail::markerAt(marked.indices, expectedHover.index);
  if (caret != nullptr && services.hoverProvider != nullptr)
    hover = services.hoverProvider->getHoverContent(
        *document, to_lsp(document->textDocument().positionAt(*caret)),
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
  } else if (content.has_value() != expectedHover.hover.has_value()) {
    // Presence first: no hover text can then pass for no hover.
    expectedFunction(
        content.has_value() ? "a hover" : "no hover",
        expectedHover.hover.has_value() ? "a hover" : "no hover",
        content.has_value() ? "the hover reads '" + *content + "'" : "");
  } else if (content.has_value()) {
    expectedFunction(*content, *expectedHover.hover);
  }

  Disposable disposable = documentDisposable(services, document);
  if (expectedHover.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
