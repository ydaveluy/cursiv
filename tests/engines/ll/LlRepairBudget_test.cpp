// A budget bounds the search, never the document: clean text stays clean,
// and a starved repair still returns a tree and reports where it was bounded.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/engines/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/engines/lr/runtime/LrLanguageParser.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/test/Check.hpp"
#include "parityll/generated/Ll/AstReflection.hpp"
#include "parityll/generated/Ll/Grammar.hpp"
#include "parityll/generated/Ll/Parser.hpp"
#include "parityll/generated/Ll/Table.hpp"
#include "paritylr/generated/Lr/AstReflection.hpp"
#include "paritylr/generated/Lr/Grammar.hpp"
#include "paritylr/generated/Lr/Parser.hpp"
#include "paritylr/generated/Lr/Table.hpp"

namespace {

constexpr std::uint64_t kAll = std::numeric_limits<std::uint64_t>::max();
constexpr cursiv::RepairBudget kBudgets[] = {
    {.floor = 0, .perByte = 0, .perSearch = 0, .perParse = 0},
    {.floor = 4096, .perByte = 0, .perSearch = 1, .perParse = 4096},
    {.floor = kAll, .perByte = kAll, .perSearch = kAll, .perParse = 128}};

std::unique_ptr<cursiv::Parser> parser(bool predictive,
                                      const cursiv::RepairBudget& budget) {
  namespace ll = tpar::generated::Ll;
  namespace lr = tpar::generated::Lr;
  if (predictive)
    return std::make_unique<cursiv::LlLanguageParser<ll::kTable>>(
        ll::kGrammarMetaData, ll::kAstActions, ll::kDrive, budget);
  return std::make_unique<cursiv::LrLanguageParser<lr::kTable>>(
      lr::kGrammarMetaData, lr::kAstActions, lr::kDrive, budget);
}

bool bounded(const cursiv::ParseResult& result) {
  return std::ranges::any_of(result.diagnostics, [](const auto& note) {
    return note.kind == cursiv::DiagnosticKind::bounded;
  });
}

void repaired(const cursiv::Parser& engine, const std::string& text,
              bool mustBeBounded) {
  const auto result = engine.parse(text);
  CHECK(result.accepted && result.value && result.cst().has_entry());
  CHECK(result.cst().recovered());
  if (mustBeBounded) CHECK(bounded(result));
  CHECK(std::ranges::any_of(result.diagnostics,
                           [](const auto& note) { return note.isSyntax(); }));
  for (const auto& note : result.diagnostics)
    CHECK(note.begin <= note.end && note.end <= text.size());
  const auto& cst = result.cst();
  for (cursiv::NodeId i = 1; i < cst.size(); ++i)
    CHECK(cst[i - 1].end <= cst[i].end);
  CHECK(cst.size() != 0 && cst[cst.size() - 1].end == text.size());
}

void a_starved_budget_keeps_clean_text_clean() {
  const std::string text = "let a = 1; show a as b;";
  for (const auto& budget : kBudgets)
    for (const bool predictive : {false, true}) {
      const auto engine = parser(predictive, budget);
      const auto result = engine->parse(text);
      CHECK(result.accepted && result.value);
      CHECK(result.diagnostics.empty() && !result.cst().recovered());
    }
}

void a_bounded_search_still_reads_the_document() {
  const std::string name(131072, 'a');
  const std::string gap(131072, ' ');
  for (const auto& budget : kBudgets)
    for (const bool predictive : {false, true}) {
      const auto engine = parser(predictive, budget);
      for (const std::string& text :
           {std::string("let a = ; show b;"),
            "let a = ; show " + name + ";",
            "let a = ;" + gap + "show b;",
            "let a = ; /*" + name + "*/ show b;"})
        repaired(*engine, text, budget.perSearch <= 1);
    }
}

void a_saturated_budget_still_respects_the_parse_ceiling() {
  std::string text;
  for (unsigned i = 0; i < 400; ++i) text += "let a = ; show b;\n";
  for (const auto& budget : {kBudgets[0], kBudgets[2]}) {
    const auto engine = parser(true, budget);
    cursiv::LlSearchWork::current() = {};
    const auto result = engine->parse(text);
    CHECK(result.accepted && result.value && bounded(result));
    CHECK(cursiv::LlSearchWork::current().work <= budget.perParse + 1024);
  }
}

void a_starved_parser_still_observes_cancellation() {
  cursiv::CancellationTokenSource source;
  source.request_stop();
  for (const bool predictive : {false, true}) {
    const auto engine = parser(predictive, kBudgets[0]);
    bool stopped = false;
    try {
      (void)engine->parse("let a = ; show b;", source.get_token());
    } catch (const cursiv::OperationCancelled&) {
      stopped = true;
    }
    CHECK(stopped);
  }
}

}  // namespace

int main() {
  a_starved_budget_keeps_clean_text_clean();
  a_bounded_search_still_reads_the_document();
  a_saturated_budget_still_respects_the_parse_ceiling();
  a_starved_parser_still_observes_cancellation();
  return cursiv::test::report("LlRepairBudget_test");
}
