#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/validation/ValidateSingleNodeOptions.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/workspace/Document.hpp"

// The pass in two halves: the built-in one -- parse diagnostics, then linking
// errors -- and the language's own checks, dispatched per node by the
// registry. The order is not cosmetic: a client reads the first diagnostic
// on a line, and a crashing check cannot cost the built-in half, which has
// already run. A document with no check still validates, since the linking
// errors have no other source.

namespace cursiv {

struct CoreServices;
struct ParseDiagnostic;
class ValidationAcceptor;
class ValidationRegistry;

// Not `final`, and its halves are protected and virtual: a language that words
// a parse repair its own way, or that anchors a broken link elsewhere,
// overrides one of `processParsingErrors`, `processLinkingErrors` or
// `validateAst` and keeps the rest.
class DefaultDocumentValidator : public DocumentValidator {
 public:
  explicit DefaultDocumentValidator(const CoreServices& core) noexcept
      : _core(core) {}

  using DocumentValidator::validateDocument;

  // Appends: the parse diagnostics, then the linking errors, then the checks'.
  void validateDocument(const Document&, std::vector<Diagnostic>&,
                        const ValidationOptions&,
                        const CancellationToken&) const override;
  ~DefaultDocumentValidator() noexcept override = default;

 protected:
  // Did the parse leave anything synthesized behind? The raw parse result
  // first, then the reflection's word on the tree, which catches nodes a
  // replaced parse closure synthesized without a sentence. Only asked when a
  // stop option is on: the sweep is O(nodes).
  [[nodiscard]] virtual bool parseLeftRepairs(const Document&,
                                              const CancellationToken&) const;
  // The parser's diagnostics in the client's shape, each stamped with its code.
  // The conversion lives here because this file owns the codes.
  virtual void processParsingErrors(const Document&, std::vector<Diagnostic>&,
                                    const CancellationToken&) const;
  // The possible repairs of a recovered error, as machine data: the spellings
  // the tokens it was owed admit, one edit row each.
  virtual void attachRepairActions(const Document&, TextRange range,
                                   const ParseDiagnostic& owed,
                                   Diagnostic&) const;
  // Every reference whose state is an error, anchored on the CST node of the
  // name. The only place a broken link becomes something a client can see.
  // The one diagnostic the parser could not write: what it refused without
  // repairing.
  [[nodiscard]] Diagnostic unrepairedParseError(const Document&) const;
  virtual void processLinkingErrors(const Document&, std::vector<Diagnostic>&,
                                    const CancellationToken&) const;
  // The linking sentence, one method for the two failures -- not found, cycle
  // -- so a language rewords them in one place. `narrow` is the site's
  // narrowing, `kNoNarrow` when it has none.
  [[nodiscard]] virtual std::string linkingErrorMessage(
      RefState why, std::uint16_t reference, std::uint8_t narrow,
      std::string_view text) const;
  // The registry's checks over every node of the tree, in document order.
  // Builds the acceptor the three halves below write through -- it lives on
  // this frame, which is what makes its sink legal for the whole walk.
  virtual void validateAst(
      const Document&, std::vector<Diagnostic>&,
      std::span<const ValidationCategory* const> categories,
      const CancellationToken&) const;

  // The three halves, separately overridable: what a hook collects is ready
  // before the first node check reads it, and what the walk accumulated is
  // whole before an after hook reports on it.
  virtual void validateAstBefore(const Document&, ValidationAcceptor&,
                                 std::span<const ValidationCategory* const>,
                                 const CancellationToken&) const;
  virtual void validateAstNodes(const Document&, ValidationAcceptor&,
                                std::span<const ValidationCategory* const>,
                                const CancellationToken&) const;
  virtual void validateAstAfter(const Document&, ValidationAcceptor&,
                                std::span<const ValidationCategory* const>,
                                const CancellationToken&) const;

  // Where a language modulates the walk, node by node: a generated region it
  // never authored is checked nowhere, a node whose contents are another
  // language's is entered nowhere. Asked once for every node, before the
  // first check runs -- the walk completes children before their parent, so
  // a subtree its root refuses has to be known in advance.
  [[nodiscard]] virtual ValidateSingleNodeOptions validateSingleNodeOptions(
      const AstNode&, std::span<const ValidationCategory* const>) const;
  // Whether the walk asks `validateSingleNodeOptions` at all. The default
  // accepts every node and costs nothing; an override of the options answers
  // true here too, or it is never asked.
  [[nodiscard]] virtual bool asksNodeOptions() const noexcept { return false; }

  // What every diagnostic of this pass is stamped with. A language that
  // publishes under another name changes this one function.
  [[nodiscard]] virtual std::string_view getSource() const noexcept;

  [[nodiscard]] const ValidationRegistry* validationRegistry() const noexcept;
  [[nodiscard]] const CoreServices& core() const noexcept { return _core; }

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
