#pragma once

#include <vector>

#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/DocumentValidator.hpp>

namespace pegium {
struct CoreServices;
}

namespace pegium::validation {

/// Default validator combining parser, linker, and custom validation checks.
class DefaultDocumentValidator : public DocumentValidator,
                                 protected pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::vector<pegium::Diagnostic>
  validateDocument(const workspace::Document &document,
                   const ValidationOptions &options,
                   const utils::CancellationToken &cancelToken) const override;

private:
  [[nodiscard]] bool runBuiltinValidation(
      const ValidationOptions &options) const noexcept;
  [[nodiscard]] bool runCustomValidation(
      const ValidationOptions &options) const noexcept;
  void processParsingErrors(const workspace::Document &document,
                            std::vector<pegium::Diagnostic> &diagnostics,
                            const utils::CancellationToken &cancelToken) const;
  void processLinkingErrors(const workspace::Document &document,
                            std::vector<pegium::Diagnostic> &diagnostics,
                            const std::string &source,
                            const utils::CancellationToken &cancelToken) const;
  void validateAst(const AstNode &rootNode,
                   std::vector<pegium::Diagnostic> &diagnostics,
                   const ValidationOptions &options, const std::string &source,
                   const utils::CancellationToken &cancelToken) const;
};

} // namespace pegium::validation
