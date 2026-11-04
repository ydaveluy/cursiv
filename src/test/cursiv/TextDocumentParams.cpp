#include "cursiv/TextDocumentParams.hpp"

#include <lsp/types.h>

#include <string>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv::test {

lsp::TextDocumentIdentifier textDocumentParams(const Document& document) {
  return {lsp::Uri::parse(document.textDocument().uri())};
}

lsp::TextDocumentPositionParams textDocumentPositionParams(
    const Document& document, TextOffset offset) {
  return {textDocumentParams(document),
          to_lsp(document.textDocument().positionAt(offset))};
}

lsp::ReferenceParams referenceParams(const Document& document,
                                     TextOffset offset,
                                     bool includeDeclaration) {
  lsp::ReferenceParams params;
  params.textDocument = textDocumentParams(document);
  params.position = to_lsp(document.textDocument().positionAt(offset));
  params.context.includeDeclaration = includeDeclaration;
  return params;
}

}  // namespace cursiv::test
