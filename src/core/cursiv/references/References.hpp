#pragma once

#include <span>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

// What a position refers to, and which references name a node. Offsets are
// bytes, not the protocol's UTF-16 units.

namespace cursiv {

struct Document;

// One shape for both places a name appears: a reference, with the declarations
// it resolved to, and a declaration's own name, with the declaration itself.
struct Declaration {
  // The name as written, not the owner's span: a construct may span several
  // words, and only one of them is under the caret.
  TextRange span;
  // A single reference yields one, a multi reference its whole set, and a
  // declaration's own name yields itself.
  std::vector<const AstNode*> targets;

  // Empty when nothing is denoted here. An unresolved reference keeps its span,
  // so rename can fix a typo while navigation finds nothing.
  [[nodiscard]] explicit operator bool() const noexcept {
    return !targets.empty();
  }
};

struct FindReferencesOptions {
  // Whether the declaration itself joins the references to it.
  bool includeDeclaration = false;
  // `kNoDocument` searches the whole workspace; document highlight restricts it
  // to one document.
  DocumentId documentId = kNoDocument;
};

class References {
 public:
  virtual ~References() noexcept = default;

  // Precondition: called under the workspace read lock. A caret one past the
  // last character is still on the word, as a CST span is half-open.
  [[nodiscard]] virtual Declaration findDeclaration(
      const Document& document, TextOffset offset,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Ordered by (document, offset), so a rename's edits come in a stable order.
  [[nodiscard]] virtual std::vector<ReferenceDescription> findReferences(
      const AstNode& target, const FindReferencesOptions& options = {},
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

namespace detail {

/**
 * Every target's rows, once each, in (document, offset) order. A site shared
 * by several targets keeps the row of the first target that reaches it.
 */
[[nodiscard]] std::vector<ReferenceDescription>
collect_references_per_declaration(
    const References& references, std::span<const AstNode* const> targets,
    function_ref<FindReferencesOptions(const AstNode&)> options,
    const CancellationToken& cancelToken);

}  // namespace detail

}  // namespace cursiv
