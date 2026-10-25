#pragma once

#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

// The inverse index as a service: what a position denotes, and who names that
// thing. It speaks byte offsets, not the protocol's UTF-16, so a provider, a
// validator and a refactoring share it with no protocol type in sight.

namespace cursiv {

struct Document;

// One shape for the two ways a name can appear: on a reference, where the
// declarations are what it resolved to, and on a declaration's own name, where
// the declaration is the node itself. A client asking to rename what is under
// the caret must not have to know which it hit.
struct Declaration {
  // The name as written, not the owner's span: a construct may span several
  // words, and only one of them is under the caret.
  TextRange span;
  // A single reference yields one, a multi reference its whole set, and a
  // declaration's own name yields itself. `const`: navigation reads a tree,
  // it never edits one.
  std::vector<const AstNode*> targets;

  // Empty is "nothing is denoted here" -- a keyword, whitespace, or a reference
  // that resolved to nothing.
  //
  // An unresolved reference still keeps its span: empty `targets` with a
  // non-empty `span` says a name is written here and denotes nothing, which is
  // the caret a user renames to fix a typo. Navigation reads the bool and stays
  // empty; rename reads the span.
  [[nodiscard]] explicit operator bool() const noexcept {
    return !targets.empty();
  }
};

struct FindReferencesOptions {
  // Whether the declaration itself joins the references to it.
  bool includeDeclaration = false;
  // `kNoDocument` is the whole workspace. Restricting to one is what makes a
  // document-highlight a find-references.
  DocumentId documentId = kNoDocument;
};

class References {
 public:
  virtual ~References() noexcept = default;

  // Precondition: called under the workspace read lock.
  //
  // A caret one past the last character is still on the word, and the retry is
  // owned here rather than by each caller: a CST span is half-open, so a
  // double-click or a word-wise motion lands one past the name it selected.
  [[nodiscard]] virtual Declaration findDeclaration(
      const Document& document, TextOffset offset,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Ordered by (document, offset), the workspace's reading order: the inverse
  // table is a hash map whose enumeration order is unspecified, and a rename's
  // edits have to be pinnable.
  [[nodiscard]] virtual std::vector<ReferenceDescription> findReferences(
      const AstNode& target, const FindReferencesOptions& options = {},
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

}  // namespace cursiv
