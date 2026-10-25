#pragma once

#include <atomic>
#include <vector>

#include "cursiv/core/references/ImportedNames.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// The cross-references of one document, and the home that answers them. Held
// by pointer and never moved: every reference stores this address and a rank
// into the list below, and `RefHome::resolve(rank)` is given nothing else. It
// is the `RefHome` itself, so reading a reference resolves lazily through it.

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

struct DocumentReferences : RefHome {
  // The work list, in the order the builder wrote it: the rank a reference
  // carries indexes this.
  std::vector<RefHandle> handles;

  ImportedNames importedNames;

  // Set when local scopes are computed, before the linking phase, and
  // published before the document's rung: a reader that sees it sees the
  // local symbols. A single reference whose read begins while its document is
  // below `ComputedScopes` -- this unset among those -- is not kept if it
  // fails: it stays `Unresolved` and the next read asks again. A plural one
  // keeps its failure, and so does a tree with no document.
  std::atomic<const Linker*> linker{nullptr};

  // ...and the document those references live in, planted with it. Every
  // site of one table shares it, so carrying it here spares the climb to
  // the root that a scope provider would otherwise pay per reference.
  std::atomic<const Document*> document{nullptr};

  // Every virtual below needs the grammar -- reading, forcing or clearing a
  // reference goes through its `RefOps` row, which only a compiled grammar
  // reaches; that is why the five live here and the linker stays erased.
  // Cancellation is polled inside the loop; an exception out of one provider
  // fails that one reference, never the list -- only cancellation propagates.
  virtual void forceAll(const CancellationToken& cancelToken) = 0;

  // The references whose name holds `offset` or ends there, forced the same
  // way: a caret asks for its own reference, not for the document's.
  virtual void forceAt(const Cst& cst, TextOffset offset,
                       const CancellationToken& cancelToken) = 0;

  // What each reference holds a target for, with the span of the text that
  // named it. The index builds its edges from this. Not visited: a reference
  // with no target, and one whose name node is gone -- an edge needs a place
  // to be drawn from. Nothing is forced: a document not yet linked yields
  // zero edges.
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
};

}  // namespace cursiv
