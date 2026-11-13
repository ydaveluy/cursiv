#pragma once

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

#include "cursiv/references/ImportedNames.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The cross-references of one document, and the `RefResolver` that resolves
// them on first read. Never moved: every reference stores this address and its
// index.

namespace cursiv {

class Cst;

struct Document;

class Linker;

// Every reference that failed: why, where its name is written, and the site as
// the linker saw it -- what a lookup for a near miss asks again.
using UnresolvedReferenceVisitor = cursiv::function_ref<void(
    RefState why, TextOffset begin, TextOffset end, const ReferenceInfo& site)>;

// One resolved edge: where its text is written, what it points at, and whether
// its source names several targets.
using ResolvedReferenceVisitor = cursiv::function_ref<void(
    TextOffset begin, TextOffset end, const AstNode& target, bool multi)>;

struct DocumentReferences : detail::RefResolver {
  // The work list, in the order the builder wrote it: the rank a reference
  // carries indexes this.
  std::vector<detail::RefHandle> handles;

  ImportedNames importedNames;

  // Set when local scopes are computed, before linking. A single reference
  // read before that is not kept if it fails, and is asked again; a plural one
  // keeps its failure, as does a tree with no document.
  std::atomic<const Linker*> linker{nullptr};

  // The document of these references, set with `linker`. Kept here so a scope
  // provider does not walk up to the root for each reference.
  std::atomic<const Document*> document{nullptr};

  // Resolves the references `which`, all or a run of them. Cancellation
  // propagates; any other exception from a provider fails that one reference.
  virtual void force(std::span<const detail::RefHandle> which,
                     const CancellationToken& cancelToken) = 0;

  // Resolves the references whose name holds `offset` or ends there: a caret
  // asks for its own reference, not for the whole document's.
  virtual void forceAt(const Cst& cst, TextOffset offset,
                       const CancellationToken& cancelToken) = 0;

  // What each reference holds a target for, with the span of the text that
  // named it. A reference with no target or no name node is skipped; nothing
  // is resolved.
  virtual void visitResolved(const Cst& cst,
                             ResolvedReferenceVisitor visitor) const = 0;

  // Back to `Unresolved`, answers included: `unlink` is the one caller.
  virtual void reset() noexcept = 0;

  // Whether any reference failed -- an error state, not merely "not resolved
  // yet": before a link, everything is unresolved and nothing has failed.
  [[nodiscard]] virtual bool anyUnresolved() const noexcept = 0;

  // Every reference that failed. A failure whose name node is gone is
  // visited with an empty span (begin == end == 0): the verdict survives the
  // place it would be underlined at.
  virtual void visitUnresolved(const Cst& cst,
                               UnresolvedReferenceVisitor visitor) const = 0;

  // The two visits above, in rank order, kept to the references whose name
  // touches `[begin, end]`. A caret is `[offset, offset]`.
  virtual void visitIn(const Cst& cst, TextOffset begin, TextOffset end,
                       ResolvedReferenceVisitor resolved,
                       UnresolvedReferenceVisitor unresolved) const = 0;
};

}  // namespace cursiv
