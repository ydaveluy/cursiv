#include <domainmodel/core/references/DomainModelScopeComputation.hpp>

#include <domainmodel/core/CoreServices.hpp>
#include <domainmodel/core/references/QualifiedNameProvider.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/utils/Cancellation.hpp>

namespace domainmodel::references {

using namespace domainmodel::ast;

std::vector<pegium::workspace::AstNodeDescription>
DomainModelScopeComputation::collectExportedSymbols(
    const pegium::workspace::Document &document,
    const pegium::utils::CancellationToken &cancelToken) const {
  if (!document.hasAst()) {
    return {};
  }

  std::vector<pegium::workspace::AstNodeDescription> symbols;
  const auto *qualifiedNameProvider = services.qualifiedNameProvider.get();
  for (const auto *node : document.parseResult.value->getAllContent()) {
    pegium::utils::throw_if_cancelled(cancelToken);
    const auto *type = pegium::ast_ptr_cast<const Type>(node);
    if (type == nullptr) {
      continue;
    }

    auto name = services.references.nameProvider->getName(*type);
    if (!name.has_value()) {
      continue;
    }

    if (const auto *package =
            pegium::ast_ptr_cast<const PackageDeclaration>(type->getContainer());
        package != nullptr && qualifiedNameProvider != nullptr) {
      *name = qualifiedNameProvider->getQualifiedName(*package, *name);
    }
    if (auto description =
            services.workspace.astNodeDescriptionProvider->createDescription(
                *type, std::move(*name), document);
        description.has_value()) {
      symbols.push_back(std::move(*description));
    }
  }
  return symbols;
}

pegium::workspace::LocalSymbols DomainModelScopeComputation::collectLocalSymbols(
    const pegium::workspace::Document &document,
    const pegium::utils::CancellationToken &cancelToken) const {
  pegium::workspace::LocalSymbols symbols;
  if (!document.hasAst()) {
    return symbols;
  }

  const auto *model = pegium::ast_ptr_cast<const DomainModel>(document.parseResult.value);
  if (model == nullptr) {
    return symbols;
  }

  (void)processContainer(*model, model->elements, document, symbols,
                         cancelToken, services.qualifiedNameProvider.get());
  return symbols;
}

std::vector<pegium::workspace::AstNodeDescription>
DomainModelScopeComputation::processContainer(
    const pegium::AstNode &container,
    const std::vector<pegium::AstNode::pointer<AbstractElement>> &elements,
    const pegium::workspace::Document &document,
    pegium::workspace::LocalSymbols &symbols,
    const pegium::utils::CancellationToken &cancelToken,
    const QualifiedNameProvider *qualifiedNameProvider) const {
  std::vector<pegium::workspace::AstNodeDescription> localDescriptions;
  localDescriptions.reserve(elements.size());

  for (const auto &element : elements) {
    pegium::utils::throw_if_cancelled(cancelToken);
    if (!element) {
      continue;
    }

    if (const auto *type = pegium::ast_ptr_cast<const Type>(element)) {
      auto name = services.references.nameProvider->getName(*type);
      if (!name.has_value()) {
        continue;
      }
      if (auto description =
              services.workspace.astNodeDescriptionProvider->createDescription(
                  *type, std::move(*name), document);
          description.has_value()) {
        localDescriptions.push_back(std::move(*description));
      }
      continue;
    }

    const auto *package = pegium::ast_ptr_cast<const PackageDeclaration>(element);
    if (package == nullptr) {
      continue;
    }

    auto nestedDescriptions = processContainer(*package, package->elements, document,
                                              symbols, cancelToken,
                                              qualifiedNameProvider);
    for (auto &nested : nestedDescriptions) {
      localDescriptions.push_back(
          createQualifiedDescription(*package, std::move(nested),
                                     qualifiedNameProvider));
    }
  }

  for (const auto &description : localDescriptions) {
    symbols.emplace(&container, description);
  }
  return localDescriptions;
}

pegium::workspace::AstNodeDescription
DomainModelScopeComputation::createQualifiedDescription(
    const PackageDeclaration &package,
    pegium::workspace::AstNodeDescription description,
    const QualifiedNameProvider *qualifiedNameProvider) {
  description.name =
      qualifiedNameProvider != nullptr
          ? qualifiedNameProvider->getQualifiedName(package.name, description.name)
          : package.name + "." + description.name;
  return description;
}

} // namespace domainmodel::references
