#include "cursiv/test/Check.hpp"

namespace rule_condition_cases { void run(); }
namespace reserved_refused_cases { void run(); }

int main() {
  rule_condition_cases::run();
  reserved_refused_cases::run();
  return cursiv::test::report("LlGuards_test");
}
