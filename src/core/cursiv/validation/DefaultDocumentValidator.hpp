#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/validation/ValidateSingleNodeOptions.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/workspace/Document.hpp"

// The pass in two halves: the built-in one, parse diagnostics then linking
// errors, and the language's checks per node. A throwing check cannot cost
// the built-in half, which runs first.

namespace cursiv {

struct CoreServices;
struct ParseDiagnostic;
class ValidationAcceptor;
class ValidationRegistry;

class DefaultDocumentValidator : public DocumentValidator,
                                 protected DefaultCoreService {
 public:
  explicit DefaultDocumentValidator(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

  // Appends: the parse diagnostics, then the linking errors, then the checks'.
  void validateDocument(const Document&, std::vector<Diagnostic>&,
                        const ValidationOptions& = {},
                        const CancellationToken& = {}) const override;
  ~DefaultDocumentValidator() noexcept override = default;

 protected:
  // The parser's diagnostics in the client's shape, each with its code.
  virtual void processParsingErrors(const Document&, std::vector<Diagnostic>&,
                                    const CancellationToken&) const;
  // The possible repairs of a recovered error, as machine data: the spellings
  // of the tokens it expected, one edit row each.
  virtual void attachRepairActions(const Document&, TextRange range,
                                   const ParseDiagnostic& parseDiagnostic,
                                   Diagnostic&) const;
  // The diagnostic for a parse the parser refused without repair.
  [[nodiscard]] Diagnostic unrepairedParseError(const Document&) const;
  // Each failed reference, in the linker's words: `Linker::createLinkingError`
  // is where a language rewords it.
  virtual void processLinkingErrors(const Document&, std::vector<Diagnostic>&,
                                    const CancellationToken&) const;
  // The registry's checks over every node of the tree. Builds the acceptor the
  // three parts below write through; it lives on this frame, so its sink stays
  // valid for the whole walk.
  virtual void validateAst(const Document&, std::vector<Diagnostic>&,
                           const ValidationOptions& options,
                           const CancellationToken&) const;

  // The three parts, separately overridable: what a hook collects is ready
  // before the first node check reads it, and what the walk accumulated is
  // whole before an after hook reports on it.
  virtual void validateAstBefore(const Document&, ValidationAcceptor&,
                                 const ValidationOptions&,
                                 const CancellationToken&) const;
  virtual void validateAstNodes(const Document&, ValidationAcceptor&,
                                const ValidationOptions&,
                                const CancellationToken&) const;
  virtual void validateAstAfter(const Document&, ValidationAcceptor&,
                                const ValidationOptions&,
                                const CancellationToken&) const;

  // The `source` of every diagnostic of this pass. A language that publishes
  // under another name overrides this one function.
  [[nodiscard]] virtual std::string_view getSource() const noexcept;

  [[nodiscard]] const ValidationRegistry* validationRegistry() const noexcept;

 private:
  // Asked for every node before the first check, from several threads on a
  // large tree: a node can skip its own checks or its whole subtree. Private:
  // an override answers every node itself, `kValidateEachNode` to keep one.
  [[nodiscard]] virtual ValidateSingleNodeOptions validateSingleNodeOptions(
      const AstNode&, const ValidationOptions&) const;
};

}  // namespace cursiv
