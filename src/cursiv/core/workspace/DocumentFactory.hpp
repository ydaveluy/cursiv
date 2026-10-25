#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

// Where a URI becomes a parsed document: the URI normalized, the language
// routed through the registry, the bytes read once, the tree cut from them.
//
// It creates and does not store: the store is `Documents`, and a factory that
// installed into it would be the cycle both refuse.

namespace cursiv {

class DocumentFactory {
 public:
  virtual ~DocumentFactory() noexcept = default;

  [[nodiscard]] virtual std::shared_ptr<Document> fromTextDocument(
      TextDocument textDocument,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  [[nodiscard]] virtual std::shared_ptr<Document> fromString(
      std::string text, std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // A tree built in memory, not parsed: `model` owns it, the text is empty,
  // and it stands at `Parsed`. With no tree at all, the builder parses it only
  // once `attachTextDocument` takes it back to `Changed`.
  [[nodiscard]] virtual std::shared_ptr<Document> fromModel(
      ParseResult model, std::string_view uri) const = 0;

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

  // The bytes move, the document does not: an edit keeps the same object and
  // the same id, which is what lets the index rows describing it be
  // refreshed rather than left pointing at something destroyed -- the reason
  // `Documents::addDocument` refuses a URI it already holds.
  //
  // It takes the document back to `Changed` and parses nothing: the rebuild
  // is the builder's. Public: cursiv has no editor-side text store to reach
  // the latest text through, so the storey that holds the new text calls
  // this directly.
  virtual Document& attachTextDocument(Document& document,
                                       TextDocument textDocument) const = 0;

  // The client's version over bytes that did not move. An open of a document
  // the scan already read leaves the disk's version 0 on it otherwise, and
  // that number goes back out on every diagnostic and every stamped edit.
  virtual Document& attachVersion(Document& document,
                                  std::int64_t version) const = 0;

 protected:
  // The one constructor behind every entry point: override it to hand out a
  // specialised document. The URI is already normalised.
  [[nodiscard]] virtual std::shared_ptr<Document> createDocument(
      TextDocument textDocument, std::string_view uri) const;

  // The write surface of a document's rebuild, for implementations only: a
  // derived factory is not the `Document`'s friend, this base is.
  static void resetForRebuild(Document& document) noexcept {
    document.resetForRebuild();
  }

  // The text a document owns, writable: `TextDocument::update` is what applies
  // the changes, and only this storey may hand it the document's own.
  [[nodiscard]] static TextDocument& textDocumentOf(
      Document& document) noexcept {
    return document._textDocument;
  }
};

}  // namespace cursiv
