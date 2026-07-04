#pragma once

#include <vector>

#include <domainmodel/core/CoreServices.hpp>
#include <domainmodel/core/ast.hpp>
#include <domainmodel/core/references/QualifiedNameProvider.hpp>

#include <pegium/core/references/DefaultScopeComputation.hpp>

namespace domainmodel::references {

class DomainModelScopeComputation final
    : public pegium::references::DefaultScopeComputation {
public:
  /// `services` shadows the base back-reference with the language's typed
  /// container, so the same computation is wired headless and in the LSP.
  explicit DomainModelScopeComputation(const DomainModelCoreServices &services)
      : pegium::references::DefaultScopeComputation(services),
        services(services) {}

  std::vector<pegium::workspace::AstNodeDescription>
  collectExportedSymbols(
      const pegium::workspace::Document &document,
      const pegium::utils::CancellationToken &cancelToken) const override;

  pegium::workspace::LocalSymbols collectLocalSymbols(
      const pegium::workspace::Document &document,
      const pegium::utils::CancellationToken &cancelToken) const override;

private:
  std::vector<pegium::workspace::AstNodeDescription> processContainer(
      const pegium::AstNode &container,
      const std::vector<pegium::AstNode::pointer<ast::AbstractElement>> &elements,
      const pegium::workspace::Document &document,
      pegium::workspace::LocalSymbols &symbols,
      const pegium::utils::CancellationToken &cancelToken,
      const QualifiedNameProvider *qualifiedNameProvider) const;

  [[nodiscard]] static pegium::workspace::AstNodeDescription
  createQualifiedDescription(
      const ast::PackageDeclaration &package,
      pegium::workspace::AstNodeDescription description,
      const QualifiedNameProvider *qualifiedNameProvider);

  const DomainModelCoreServices &services;
};

} // namespace domainmodel::references
