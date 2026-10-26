#include "cursiv/test/Check.hpp"

namespace indirect_fold_cases { void run(); }
namespace prefix_factor_cases { void run(); }
namespace infix_loop_cases { void run(); }
namespace operator_condition_cases { void run(); }
namespace opening_token_cases { void run(); }

int main() {
  indirect_fold_cases::run();
  prefix_factor_cases::run();
  infix_loop_cases::run();
  operator_condition_cases::run();
  opening_token_cases::run();
  return cursiv::test::report("LlExpression_test");
}
