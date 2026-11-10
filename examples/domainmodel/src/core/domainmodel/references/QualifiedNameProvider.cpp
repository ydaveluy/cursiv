#include "domainmodel/references/QualifiedNameProvider.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/Document.hpp"

namespace dm {

// The walk is the qualifier: a package nested in a package answers with both,
// outermost first. A loop, and one string built once, however deep the
// packages nest.
std::string QualifiedNameProvider::qualifierOf(
    const cursiv::Document&, const ast::PackageDeclaration& package) const {
  // A single component needs no reversed collection.
  if (package.type == &cursiv::ast_type_tag<ast::PackageDeclaration>
      && (package.parent == nullptr
          || package.parent->type
                 != &cursiv::ast_type_tag<ast::PackageDeclaration>))
    return std::string(package.name);

  std::vector<std::string_view> names;
  std::size_t size = 0;
  for (const cursiv::AstNode* at = &package;
       at != nullptr
       && at->type == &cursiv::ast_type_tag<ast::PackageDeclaration>;
       at = at->parent) {
    names.push_back(static_cast<const ast::PackageDeclaration*>(at)->name);
    size += names.back().size() + 1;
  }
  std::string qualifier;
  qualifier.reserve(size);
  for (auto name = names.rbegin(); name != names.rend(); ++name) {
    if (!qualifier.empty()) qualifier += '.';
    qualifier += *name;
  }
  return qualifier;
}

std::string QualifiedNameProvider::getQualifiedName(
    const cursiv::Document& document, const ast::PackageDeclaration& qualifier,
    std::string_view name) const {
  std::string qualified = qualifierOf(document, qualifier);
  if (!qualified.empty()) {
    qualified.reserve(qualified.size() + name.size() + 1);
    qualified.push_back('.');
  }
  qualified.append(name);
  return qualified;
}

std::string QualifiedNameProvider::getQualifiedName(
    std::string_view qualifier, std::string_view name) const {
  if (qualifier.empty()) return std::string(name);
  std::string qualified;
  qualified.reserve(qualifier.size() + name.size() + 1);
  qualified.append(qualifier);
  qualified.push_back('.');
  qualified.append(name);
  return qualified;
}

std::string_view QualifiedNameProvider::keepQualifiedName(
    const cursiv::Document& document, const ast::PackageDeclaration& qualifier,
    std::string_view name) const {
  if (qualifier.type == &cursiv::ast_type_tag<ast::PackageDeclaration>
      && (qualifier.parent == nullptr
          || qualifier.parent->type
                 != &cursiv::ast_type_tag<ast::PackageDeclaration>))
    return keepQualifiedName(document, qualifier.name, name);
  // The temporary qualifier remains alive while keep_text copies its parts.
  return keepQualifiedName(document, qualifierOf(document, qualifier), name);
}

std::string_view QualifiedNameProvider::keepQualifiedName(
    const cursiv::Document& document, std::string_view qualifier,
    std::string_view name) const {
  if (qualifier.empty()) return document.keep_text(name);
  return document.keep_text(qualifier, ".", name);
}

}  // namespace dm
