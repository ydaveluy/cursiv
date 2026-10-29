// Installs the services of the @CURSIV_NEW_ID@ language: first the parser and
// the other services generated from the grammar, then your own, such as the
// validation checks.

#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"
#include "@CURSIV_NEW_ID@/generated/@CURSIV_NEW_CLASS@/Module.hpp"

namespace @CURSIV_NEW_ID@ {

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services) {
  @CURSIV_NEW_ID@::generated::@CURSIV_NEW_CLASS@::install_generated_module(services);
  register_validation_checks(services);
}

}  // namespace @CURSIV_NEW_ID@
