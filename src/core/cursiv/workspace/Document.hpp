#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/LocalSymbols.hpp"
#include "cursiv/workspace/TextDocument.hpp"

// A document holds what was derived from one text: the trees a parse made,
// the diagnostics a validation published, and how far along that ladder it
// stands. The fields are public and mutable, each phase of a build writing
// the one it computes. Its language is resolved once, at creation.

namespace cursiv {

class DocumentFactory;
struct CoreServices;

// It is an `AstNode`, and that is what makes a node's document reachable: the
// root's `parent` is the document, so climbing to the top of a tree lands on
// it -- no per-node back-pointer, and no root-to-document table for a builder
// to keep current. Its own `type` tag is what proves the cast.
struct Document : AstNode {
  friend class DocumentFactory;

  DocumentId id = kNoDocument;
  // Canonical, and stable for the document's whole lifetime: the text may be
  // replaced, the name it is known by may not.
  const std::string uri;

  // Resolved once, by the factory, from the URI and the client's languageId
  // together -- the two are only both in hand there. A reader that resolved it
  // again would have to remember the id, and every reader that forgot would
  // run another language's services over this tree.
  const CoreServices* language = nullptr;

  // Read while a build advances it, hence the atomic; the transitions
  // themselves are ordered by the build's own phases.
  std::atomic<DocumentState> state = DocumentState::Changed;

  ParseResult parseResult;

  // What the scope computation found: empty below `ComputedScopes`.
  LocalSymbols localSymbols;

  std::vector<Diagnostic> diagnostics;

  // Bumped whenever a rebuild tears down the previous build's derived state.
  // Not the text's version, which a re-read from disk stamps back to 0: this
  // moves on every rebuild, so a key minted against a build that no longer
  // exists can be told apart.
  std::uint64_t buildRevision = 0;

  // Whether a client is looking at it. The open documents climb every rung
  // first, so an editor sees its own file before the rest of the store.
  bool isOpen = false;

  [[nodiscard]] bool parseSucceeded() const noexcept;
  [[nodiscard]] bool hasAst() const noexcept;
  // Syntax only: a conversion that failed is not a document that was repaired.
  [[nodiscard]] bool parseRecovered() const noexcept;

  // Whether any reference of this document failed to resolve. What repairs a
  // broken link when the missing symbol turns up elsewhere: a document that
  // still owes one is rebuilt even when nothing it names has moved.
  [[nodiscard]] bool hasLinkingErrors() const noexcept;

  [[nodiscard]] const TextDocument& textDocument() const noexcept;

  // Keeps a name a scope computation built, for a description that views it: it
  // lives until the next parse. The parts are written in one piece, so
  // `keep_text(outer, ".", inner)` needs no temporary. Every call adds to the
  // document's memory, so call it from a scope computation, which the builder
  // runs once per parse; a reset to `IndexedContent` or `Parsed` runs it again
  // on the same parse. It is not locked: the caller holds the document's write.
  template <std::convertible_to<std::string_view>... Parts>
    requires(sizeof...(Parts) != 0)
  std::string_view keep_text(const Parts&... parts) const {
    const std::string_view views[]{std::string_view(parts)...};
    return keep_parts(views);
  }

  explicit Document(TextDocument textDocument, std::string uri = {});
  ~Document();
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  Document(Document&&) = delete;
  Document& operator=(Document&&) = delete;

 private:
  std::string_view keep_parts(std::span<const std::string_view> parts) const;
  void resetForRebuild() noexcept;

  TextDocument _textDocument;
};

}  // namespace cursiv
