#pragma once

#include <string>
#include <string_view>

#include "cursiv/services/SharedCoreServices.hpp"
#include "domainmodel/ast.hpp"

// The name a declaration answers to from outside its package: `foo.bar.E2`
// rather than `E2`. The language's own service: what a qualifier is belongs to
// the grammar.

namespace dm {

class QualifiedNameProvider {
 public:
  explicit QualifiedNameProvider(cursiv::SharedCoreServices&) {}

  // The package's own qualified name, walked up to the root, then the name.
  [[nodiscard]] std::string getQualifiedName(
      const cursiv::Document& document,
      const ast::PackageDeclaration& qualifier, std::string_view name) const;

  [[nodiscard]] std::string getQualifiedName(std::string_view qualifier,
                                             std::string_view name) const;

  // Scope names borrow their bytes until the document's next parse. Write
  // directly to its pool instead of creating a string to copy there.
  [[nodiscard]] std::string_view keepQualifiedName(
      const cursiv::Document& document,
      const ast::PackageDeclaration& qualifier, std::string_view name) const;

  [[nodiscard]] std::string_view keepQualifiedName(
      const cursiv::Document& document, std::string_view qualifier,
      std::string_view name) const;

 private:
  [[nodiscard]] std::string qualifierOf(
      const cursiv::Document& document,
      const ast::PackageDeclaration& package) const;
};

}  // namespace dm
