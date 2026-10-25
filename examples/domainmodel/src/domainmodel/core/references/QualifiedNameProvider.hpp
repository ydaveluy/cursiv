#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "domainmodel/core/ast.hpp"

// The name a declaration answers to from outside its package: `foo.bar.E2`
// rather than `E2`. This is the language's own service, because what a
// qualifier is belongs to the grammar and not to the framework.

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

 private:
  // The climb, recomputed every call -- NOT memoised: measurement found a
  // `DocumentCache` lookup slower than just climbing, since the chain is one
  // or two levels and a cached hit still costs a hash, a lock, and a copy.
  [[nodiscard]] std::string qualifierOf(
      const cursiv::Document& document,
      const ast::PackageDeclaration& package) const;
};

}  // namespace dm
