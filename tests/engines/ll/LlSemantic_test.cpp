// Semantic, completion and trivia cases share one generated language.
#include <cstdint>
#include <string>

#include "cursiv/Check.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/ll/runtime/ll_predictor.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "semanticll/generated/Ll/AstReflection.hpp"
#include "semanticll/generated/Ll/Grammar.hpp"
#include "semanticll/generated/Ll/Parser.hpp"
#include "semanticll/generated/Ll/Table.hpp"

namespace ll_caret_agreement_cases { void run(); }
namespace ll_caret_depth_cases { void run(); }
namespace ll_creation_boundary_cases { void run(); }
namespace ll_data_text_cases { void run(); }
namespace ll_fold_order_cases { void run(); }
namespace ll_keyword_relay_cases { void run(); }
namespace ll_reserved_word_cases { void run(); }
namespace ll_skip_configs_cases { void run(); }
namespace ll_text_order_cases { void run(); }

namespace ll_staged_stop_cases {

namespace ll = tllsemantic::generated::Ll;

cursiv::CancellationTokenSource* stopOnEntry = nullptr;
std::uint64_t readAhead = 0;

// The descent with a stop requested as it starts, and the tokens the generated
// automaton read ahead before the stop was read.
void stopping_drive(cursiv::engine::LlContext& c) {
  if (stopOnEntry != nullptr) stopOnEntry->request_stop();
  const std::uint64_t before = cursiv::engine::ll_staged_reads<ll::kTable>();
  try {
    ll::kDrive(c);
  } catch (const cursiv::OperationCancelled&) {
    readAhead = cursiv::engine::ll_staged_reads<ll::kTable>() - before;
    throw;
  }
}

// A stop is read between the tokens the generated automaton reads ahead of a
// choice, however long the run its arms share.
void a_stop_is_read_while_the_automaton_reads_ahead() {
  const cursiv::engine::LlLanguageParser<ll::kTable> parser(
      ll::kGrammarMetaData, ll::kAstActions, stopping_drive);
  std::string run = "staged:";
  for (unsigned i = 0; i < 100000; ++i) run += " x";
  const std::string text = run + " !";
  const std::string other = run + " y ;";
  CHECK(parser.parse(text).diagnostics.empty());
  CHECK(parser.parse(other).diagnostics.empty());
  cursiv::CancellationTokenSource source;
  stopOnEntry = &source;
  bool threw = false;
  try {
    (void)parser.parse(text, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  stopOnEntry = nullptr;
  CHECK(threw);
  CHECK(readAhead <= cursiv::engine::CancellationPoll::kTurns);
}

}  // namespace ll_staged_stop_cases

int main() {
  ll_staged_stop_cases::a_stop_is_read_while_the_automaton_reads_ahead();
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
