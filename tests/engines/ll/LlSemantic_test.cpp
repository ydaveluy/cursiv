// Semantic, completion and trivia cases share one generated language.
#include "cursiv/test/Check.hpp"
namespace ll_caret_agreement_cases { void run(); }
namespace ll_caret_depth_cases { void run(); }
namespace ll_creation_boundary_cases { void run(); }
namespace ll_data_text_cases { void run(); }
namespace ll_fold_order_cases { void run(); }
namespace ll_keyword_relay_cases { void run(); }
namespace ll_reserved_word_cases { void run(); }
namespace ll_skip_configs_cases { void run(); }
namespace ll_text_order_cases { void run(); }

int main() {
  ll_caret_agreement_cases::run();
  ll_caret_depth_cases::run();
  ll_creation_boundary_cases::run();
  ll_data_text_cases::run();
  ll_fold_order_cases::run();
  ll_keyword_relay_cases::run();
  ll_reserved_word_cases::run();
  ll_skip_configs_cases::run();
  ll_text_order_cases::run();
  return cursiv::test::report("LlSemantic_test");
}
