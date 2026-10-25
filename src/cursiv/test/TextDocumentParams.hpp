// The request a client would have sent, built from a document and an offset:
// the providers of this tree take a position rather than the envelope, so what
// these build is what a consumer driving the protocol itself needs.
#pragma once

#include <lsp/types.h>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {
struct Document;
}  // namespace cursiv

namespace cursiv::test {

[[nodiscard]] lsp::TextDocumentIdentifier textDocumentParams(
    const Document& document);

[[nodiscard]] lsp::TextDocumentPositionParams textDocumentPositionParams(
    const Document& document, TextOffset offset);

[[nodiscard]] lsp::ReferenceParams referenceParams(const Document& document,
                                                   TextOffset offset,
                                                   bool includeDeclaration);

}  // namespace cursiv::test
