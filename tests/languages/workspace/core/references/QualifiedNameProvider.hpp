#pragma once

#include <string>
#include <string_view>

#include "cursiv/services/SharedCoreServices.hpp"
#include "workspace/core/ast.hpp"

// The name a declaration has outside its package: `foo.bar.E2` rather than
// `E2`.

namespace ws {

class QualifiedNameProvider {
 public:
  explicit QualifiedNameProvider(cursiv::SharedCoreServices&) {}

  // `name` qualified by `qualifier` and the packages that enclose it.
  [[nodiscard]] std::string getQualifiedName(
      const cursiv::Document& document,
      const ast::PackageDeclaration& qualifier, std::string_view name) const;

  [[nodiscard]] std::string getQualifiedName(std::string_view qualifier,
                                             std::string_view name) const;

 private:
  [[nodiscard]] std::string qualifierOf(
      const cursiv::Document& document,
      const ast::PackageDeclaration& package) const;
};

}  // namespace ws
