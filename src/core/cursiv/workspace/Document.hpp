#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/LocalSymbols.hpp"
#include "cursiv/workspace/TextDocument.hpp"

// What was derived from one text: the parse, the diagnostics, and the state
// reached. Each build phase writes its own public field. The language is
// resolved once, at creation.

namespace cursiv {

class DocumentFactory;
struct CoreServices;

// A document is the root's `parent`, so walking up a tree ends on it: no
// per-node back-pointer is needed. Its `type` tag proves the cast.
struct Document : AstNode {
  friend class DocumentFactory;

  DocumentId id = kNoDocument;
  // Canonical, and stable for the document's whole lifetime: the text may be
  // replaced, the name it is known by may not.
  const std::string uri;

  // Resolved once, by the factory, from the URI and the client's languageId.
  const CoreServices* language = nullptr;

  // Read while a build advances it, hence the atomic; the transitions
  // themselves are ordered by the build's own phases.
  std::atomic<DocumentState> state = DocumentState::Changed;

  ParseResult parseResult;

  // What the scope computation found: empty below `ComputedScopes`.
  LocalSymbols localSymbols;

  std::vector<Diagnostic> diagnostics;

  // Bumped on every rebuild, unlike the text's version, so a key made for an
  // older build can be told apart.
  std::uint64_t buildRevision = 0;

  // Whether a client is looking at it. The open documents reach every state
  // first, so an editor sees its own file before the rest of the store.
  bool isOpen = false;

  [[nodiscard]] bool hasAst() const noexcept;
  // Syntax only: a conversion that failed is not a document that was repaired.
  [[nodiscard]] bool parseRecovered() const noexcept;

  // Whether any reference of this document failed to resolve. What repairs a
  // broken link when the missing symbol turns up elsewhere: a document that
  // still has one is rebuilt even when nothing it names has moved.
  [[nodiscard]] bool hasLinkingErrors() const noexcept;

  [[nodiscard]] const TextDocument& textDocument() const noexcept;

  // Keeps a name a scope computation built until the next parse, written in one
  // piece. Each call adds memory, so call it once per parse. Not locked: the
  // caller holds the document's write.
  template <std::convertible_to<std::string_view>... Parts>
    requires(sizeof...(Parts) != 0)
  std::string_view keepText(const Parts&... parts) const {
    const std::string_view views[]{std::string_view(parts)...};
    return keepParts(views);
  }

  explicit Document(TextDocument textDocument, std::string uri = {});
  ~Document();
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  Document(Document&&) = delete;
  Document& operator=(Document&&) = delete;

 private:
  std::string_view keepParts(std::span<const std::string_view> parts) const;
  void resetForRebuild();

  TextDocument _textDocument;
};

}  // namespace cursiv
