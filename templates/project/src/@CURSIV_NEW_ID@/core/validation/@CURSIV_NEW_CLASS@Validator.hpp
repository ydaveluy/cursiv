// The validation checks of the @CURSIV_NEW_ID@ language: rules that the
// grammar alone cannot express. Declare a check here, write it in the .cpp
// file and register it in `register_validation_checks`.
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

// Creates the validator and registers its checks; `install_core_module` calls
// it.
struct @CURSIV_NEW_CLASS@CoreServices;
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
