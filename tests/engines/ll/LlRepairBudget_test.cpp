// A budget bounds the search, never the document: clean text stays clean,
// and a starved repair still returns a tree and reports where it was bounded.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/lr/runtime/LrLanguageParser.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/Check.hpp"
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
constexpr cursiv::engine::RepairBudget kBudgets[] = {
    {.floor = 0, .perByte = 0, .perSearch = 0, .perParse = 0},
    {.floor = 4096, .perByte = 0, .perSearch = 1, .perParse = 4096},
    {.floor = kAll, .perByte = kAll, .perSearch = kAll, .perParse = 128}};

std::unique_ptr<cursiv::Parser> parser(
    bool predictive, const cursiv::engine::RepairBudget& budget) {
  namespace ll = tpar::generated::Ll;
  namespace lr = tpar::generated::Lr;
  if (predictive)
    return std::make_unique<cursiv::engine::LlLanguageParser<ll::kTable>>(
        ll::kGrammarMetaData, ll::kAstActions, ll::kDrive, budget);
  return std::make_unique<cursiv::engine::LrLanguageParser<lr::kTable>>(
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
    cursiv::engine::LlSearchWork::current() = {};
    const auto result = engine->parse(text);
    CHECK(result.accepted && result.value && bounded(result));
    CHECK(cursiv::engine::LlSearchWork::current().work <=
          budget.perParse + 1024);
  }
}

// Two repairs of one fault merge at the next token, one having read a token
// more than the other: from there they read as one, so the text behind the
// fault costs the search nothing, however long it is.
void the_text_behind_a_repair_costs_nothing() {
  std::string text = "show 1 + , 2;\n";
  for (unsigned i = 0; i < 40000; ++i) text += "show 3;\n";
  for (const bool predictive : {false, true}) {
    const auto engine = parser(predictive, cursiv::engine::RepairBudget{});
    cursiv::engine::LlSearchWork::current() = {};
    const auto result = engine->parse(text);
    CHECK(result.accepted && result.value && !bounded(result));
    CHECK(result.diagnostics.size() == 1);
    if (predictive) CHECK(cursiv::engine::LlSearchWork::current().work < 4096);
  }
}

// A work count, one diagnostic and no bound: what a fault in front of `tail`
// must come to, however long the stretch it has to read past.
void one_cheap_repair(const std::string& text, std::uint64_t most) {
  for (const bool predictive : {false, true}) {
    const auto engine = parser(predictive, cursiv::engine::RepairBudget{});
    cursiv::engine::LlSearchWork::current() = {};
    const auto result = engine->parse(text);
    CHECK(result.accepted && result.value && !bounded(result));
    CHECK(result.diagnostics.size() == 1);
    if (predictive) CHECK(cursiv::engine::LlSearchWork::current().work < most);
  }
}

std::string long_sum() {
  std::string sum = "(2";
  for (unsigned i = 0; i < 20000; ++i) sum += " + 2";
  return sum + ")";
}

// Arms that part only behind the sum: a repair in front of it holds once a plan
// replay could read no further, and need not read on to tell them apart.
void arms_that_part_late_do_not_cost_the_stretch() {
  one_cheap_repair("rep:either , " + long_sum() + ";\n", 4096);
}

// Two repairs that stand in different stacks cannot merge before the bracket
// closes: the race between them stops well short of its end.
void a_race_does_not_cost_the_bracket() {
  one_cheap_repair("rep:sums 1 + , " + long_sum() + ";\n", 32768);
}

// A fault behind a long right-recursive list: every call of the list ends where
// the last one does, so a reading stands on all of them at once, and the search
// pays for each once.
void a_fault_behind_a_long_list_does_not_cost_its_square() {
  std::string text = "rep:plain ";
  for (unsigned i = 0; i < 4000; ++i) text += "1,";
  one_cheap_repair(text + "1 ? ! ;\n", 262144);
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

// Past the budget at the end of the text, the table engine gives up the
// declaration the fault stands in and no other; the descent closes it. The
// declarations before it keep the records of the text without it.
void an_unclosed_last_declaration_costs_no_other() {
  for (const std::string_view before : {"let a = 1; let b = 2; ", ""})
    for (const std::string_view tail :
         {"let c = (1 + ", "let c = (((", "let c = f(1, "})
      for (const bool predictive : {false, true}) {
        const std::string text = std::string(before) + std::string(tail);
        const auto engine = parser(predictive, kBudgets[0]);
        const auto whole = engine->parse(before);
        const auto cut = engine->parse(text);
        CHECK(whole.accepted && whole.diagnostics.empty() && cut.accepted);
        const auto& was = whole.cst();
        const auto& is = cut.cst();
        bool kept = is.size() > was.size();
        for (cursiv::NodeId i = 0; kept && i + 1 < was.size(); ++i)
          kept = is[i].begin == was[i].begin && is[i].end == was[i].end &&
                 is[i].subtreeSize == was[i].subtreeSize && !is[i].recovered;
        CHECK(kept);
        if (predictive) continue;
        CHECK(is[is.size() - 1].recovered == before.empty());
        CHECK(cut.diagnostics.size() == 2 &&
              cut.diagnostics[0].kind == cursiv::DiagnosticKind::incomplete &&
              cut.diagnostics[0].begin == before.size() &&
              cut.diagnostics[1].kind == cursiv::DiagnosticKind::bounded &&
              cut.diagnostics[1].begin == before.size());
      }
}

// A search stopped short of any goal follows the reading that went past the
// most of the author's tokens: a quoted text reading the rest of the document
// is one token, the two stray quotes dropped are two. One floor per engine.
void a_long_token_does_not_win_a_search_without_a_goal() {
  const std::string text = "say:say a = 'b'; say c = d; say e = f; say g = h;";
  for (const auto& [predictive, floor] : {std::pair{false, std::uint64_t{48}},
                                          std::pair{true, std::uint64_t{96}}}) {
    const auto engine =
        parser(predictive, {.floor = floor, .perByte = 0, .perSearch = kAll,
                            .perParse = kAll});
    const auto result = engine->parse(text);
    CHECK(result.accepted && bounded(result));
    std::vector<cursiv::ParseDiagnostic> edits;
    for (const auto& note : result.diagnostics)
      if (note.kind != cursiv::DiagnosticKind::bounded) edits.push_back(note);
    CHECK(edits.size() == 2 &&
          edits[0].kind == cursiv::DiagnosticKind::deleted &&
          edits[0].begin == 12 && edits[0].end == 13 &&
          edits[1].kind == cursiv::DiagnosticKind::deleted &&
          edits[1].begin == 14 && edits[1].end == 15);
  }
}

}  // namespace

int main() {
  a_starved_budget_keeps_clean_text_clean();
  a_bounded_search_still_reads_the_document();
  a_saturated_budget_still_respects_the_parse_ceiling();
  the_text_behind_a_repair_costs_nothing();
  arms_that_part_late_do_not_cost_the_stretch();
  a_race_does_not_cost_the_bracket();
  a_fault_behind_a_long_list_does_not_cost_its_square();
  a_starved_parser_still_observes_cancellation();
  an_unclosed_last_declaration_costs_no_other();
  a_long_token_does_not_win_a_search_without_a_goal();
  return cursiv::test::report("LlRepairBudget_test");
}
