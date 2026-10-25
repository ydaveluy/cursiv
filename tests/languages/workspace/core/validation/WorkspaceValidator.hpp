#pragma once

#include <vector>

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/utils/DocumentCache.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "workspace/core/ast.hpp"

// A method's first parameter is the type it watches, so the list in
// `CoreModule.cpp` is the whole registration. The cycle check exists because
// an `extends` cycle hangs every reader of the hierarchy.

namespace ws {

class WorkspaceValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkEntityNameStartsWithCapital(
      const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const;
  void checkDataTypeNameStartsWithCapital(
      const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const;
  void checkEntityDoesNotExtendItself(const ast::Entity& entity,
                                      cursiv::ValidationAcceptor& accept) const;

 private:
  // Whether the climb from `entity` ends. `seen` is the current climb, which is
  // what tells a cycle from a chain already answered.
  [[nodiscard]] bool terminates(const ast::Entity& entity,
                                std::vector<const ast::Entity*>& seen) const;

  // Whether the climb from this entity ends, memoised -- unmemoised this
  // check is quadratic over a document. Keyed by document, so a rebuild
  // drops exactly the entries it invalidates.
  cursiv::DocumentCache<const ast::Entity*, bool> _terminates{services.shared};
};

}  // namespace ws
