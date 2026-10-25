// What this language installs over the generated module: what its author
// writes. Everything the grammar derives -- parser, reflection, metadata -- is
// in the generated one, and this file is the only place the two meet.

#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"
#include "@CURSIV_NEW_ID@/generated/@CURSIV_NEW_CLASS@/Module.hpp"

namespace @CURSIV_NEW_ID@ {

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services) {
  @CURSIV_NEW_ID@::generated::@CURSIV_NEW_CLASS@::install_generated_module(services);
  register_validation_checks(services);
}

}  // namespace @CURSIV_NEW_ID@
