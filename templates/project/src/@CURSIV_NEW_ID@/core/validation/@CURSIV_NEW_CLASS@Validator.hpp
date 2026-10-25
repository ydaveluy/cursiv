// The checks of this language: what the grammar accepts but the language does
// not. Two here -- a naming convention, and the one fact no single node can see.
#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

namespace @CURSIV_NEW_ID@ {

class @CURSIV_NEW_CLASS@Validator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkPersonStartsWithCapital(const ast::Person& person,
                                    cursiv::ValidationAcceptor& accept) const;
  void checkUniquePersons(const ast::Model& model,
                          cursiv::ValidationAcceptor& accept) const;
};

// Declared here rather than in the module, so a check and its registration are
// read together. `CoreServices.hpp` is the one that includes this header.
struct @CURSIV_NEW_CLASS@CoreServices;
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
