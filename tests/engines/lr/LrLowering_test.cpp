// Construction, lowering and generated pass cases share their engine headers.
#include "cursiv/test/Check.hpp"

namespace lr_construction_cases { void run(); }
namespace lr_lowered_cases { void run(); }
namespace lr_passes_cases { void run(); }
#ifdef CURSIV_TEST_NATIVE_IDENTITY
namespace lr_identity_cases { void run(); }
#endif

int main() {
  lr_construction_cases::run();
  lr_lowered_cases::run();
  lr_passes_cases::run();
#ifdef CURSIV_TEST_NATIVE_IDENTITY
  lr_identity_cases::run();
#endif
  return cursiv::test::report("LrLowering_test");
}
