#include "workspace/core/references/QualifiedNameProvider.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/Document.hpp"

namespace ws {

// The names of `package` and the packages enclosing it, outermost first.
std::string QualifiedNameProvider::qualifierOf(
    const cursiv::Document&, const ast::PackageDeclaration& package) const {
  std::vector<std::string_view> names;
  std::size_t size = 0;
  for (const cursiv::AstNode* at = &package;
       at != nullptr &&
       at->type == &cursiv::ast_type_tag<ast::PackageDeclaration>;
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
  return getQualifiedName(qualifierOf(document, qualifier), name);
}

std::string QualifiedNameProvider::getQualifiedName(
    std::string_view qualifier, std::string_view name) const {
  if (qualifier.empty()) return std::string(name);
  return std::string(qualifier) + "." + std::string(name);
}

}  // namespace ws
