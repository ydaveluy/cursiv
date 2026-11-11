#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

// Where a URI becomes a document: the URI normalized, the language routed
// through the registry, the bytes read once. It creates but never stores:
// the store is `Documents`. `fromUri` and `update` run on the pool.

namespace cursiv {

class DocumentFactory {
 public:
  virtual ~DocumentFactory() noexcept = default;

  // The document `textDocument` becomes, at `Changed` with its URI normalized
  // and its language resolved. A build parses it through `update`. Override it
  // to hand out a specialized document. Throws when no language claims the URI.
  [[nodiscard]] virtual std::shared_ptr<Document> create(
      TextDocument textDocument) const = 0;

  [[nodiscard]] virtual std::shared_ptr<Document> fromTextDocument(
      TextDocument textDocument,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  [[nodiscard]] virtual std::shared_ptr<Document> fromString(
      std::string text, std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // The file behind `uri`, read once. Throws when no language claims it or
  // nothing readable is there.
  [[nodiscard]] virtual std::shared_ptr<Document> fromUri(
      std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Refresh the text from its source and parse it again, ending at `Parsed`.
  // The builder calls this only for documents below that state.
  virtual Document& update(
      Document& document,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Replaces the text of the same document object, so its id and index rows
  // stay, and takes it back to `Changed`: the builder rebuilds it. Called by
  // the layer that holds the new text.
  virtual Document& attachTextDocument(Document& document,
                                       TextDocument textDocument) const = 0;

  // Applies the changes to the document's own text, in place, and takes it back
  // to `Changed`. False, with nothing touched, when a lone change rewrites its
  // range with the same bytes or there is no change.
  virtual bool updateTextDocument(
      Document& document,
      std::span<const TextDocumentContentChangeEvent> changes,
      std::int64_t version) const = 0;

  // The client's version over bytes that did not move. An open of a document
  // the scan already read leaves the disk's version 0 on it otherwise, and
  // that number goes back out on every diagnostic and every edit.
  virtual Document& attachVersion(Document& document,
                                  std::int64_t version) const = 0;

 protected:
  // The write surface of a document's rebuild, for implementations only: a
  // derived factory is not the `Document`'s friend, this base is.
  static void resetForRebuild(Document& document) {
    document.resetForRebuild();
  }

  // The text a document owns, writable: `TextDocument::update` is what applies
  // the changes, and only the factory may hand it the document's own.
  [[nodiscard]] static TextDocument& textDocumentOf(
      Document& document) noexcept {
    return document._textDocument;
  }
};

}  // namespace cursiv
