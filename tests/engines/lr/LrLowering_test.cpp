// Construction, lowering and generated pass cases share their engine headers.
namespace lr_construction_cases { int run(); }
namespace lr_lowered_cases { int run(); }
namespace lr_passes_cases { int run(); }
#ifdef CURSIV_TEST_NATIVE_IDENTITY
namespace lr_identity_cases { int run(); }
#endif

int main() {
  int status = 0;
  status |= lr_construction_cases::run();
  status |= lr_lowered_cases::run();
  status |= lr_passes_cases::run();
#ifdef CURSIV_TEST_NATIVE_IDENTITY
  status |= lr_identity_cases::run();
#endif
  return status;
}
