#include "domainmodel/core/references/QualifiedNameProvider.hpp"

#include <string>
#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace dm {

// The climb is the qualifier: a package nested in a package answers with both,
// outermost first.
std::string QualifiedNameProvider::qualifierOf(
    const cursiv::Document& document,
    const ast::PackageDeclaration& package) const {
  const cursiv::AstNode* const container = package.parent;
  if (container != nullptr
      && container->type == &cursiv::ast_type_tag<ast::PackageDeclaration>)
    return getQualifiedName(
        qualifierOf(document,
                    static_cast<const ast::PackageDeclaration&>(*container)),
        package.name);
  return std::string(package.name);
}

std::string QualifiedNameProvider::getQualifiedName(
    const cursiv::Document& document, const ast::PackageDeclaration& qualifier,
    std::string_view name) const {
  return getQualifiedName(qualifierOf(document, qualifier), name);
}

std::string QualifiedNameProvider::getQualifiedName(
    std::string_view qualifier, std::string_view name) const {
  if (qualifier.empty()) return std::string(name);
  return std::string(qualifier) + "." + std::string(name);
}

}  // namespace dm
