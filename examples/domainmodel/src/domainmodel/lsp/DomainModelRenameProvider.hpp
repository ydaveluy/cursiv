#pragma once

#include <optional>
#include <string_view>

#include <domainmodel/core/references/QualifiedNameProvider.hpp>
#include <domainmodel/lsp/LspServices.hpp>

#include <pegium/lsp/navigation/DefaultRenameProvider.hpp>

namespace domainmodel {

class DomainModelRenameProvider final : public pegium::DefaultRenameProvider {
public:
  /// `services` shadows the base back-reference with the language's typed LSP
  /// container, giving typed access to both the LSP and the added services.
  explicit DomainModelRenameProvider(const DomainModelServices &services)
      : pegium::DefaultRenameProvider(services), services(services) {}

  std::optional<::lsp::WorkspaceEdit>
  rename(const pegium::workspace::Document &document,
         const ::lsp::RenameParams &params,
         const pegium::utils::CancellationToken &cancelToken) const override;

private:
  [[nodiscard]] std::optional<std::string>
  buildQualifiedName(const pegium::AstNode &node, const pegium::AstNode &renamedRoot,
                     std::string_view replacementName,
                     const domainmodel::references::QualifiedNameProvider
                         *qualifiedNameProvider) const;

  const DomainModelServices &services;
};

} // namespace domainmodel
