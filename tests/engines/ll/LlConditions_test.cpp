#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "conditionsll/generated/Ll/Module.hpp"
#include "conditionsll/generated/Ll/Table.hpp"
#include "conditionslr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionSession.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_repair.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"
#include "cursiv/engines/shared/runtime/GapPositions.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ConditionsGrammar.hpp"
#include "triviapredictll/generated/TriviaLl/Module.hpp"
#include "triviapredictll/generated/TriviaLl/Table.hpp"

namespace {

namespace cache_test {
constexpr const auto& base = tllconditions::generated::Ll::kTable;
constexpr auto identifier = [] {
  for (std::uint16_t i = 1; i < base.nterms; ++i)
    if (base.term_name(i) == "ID") return i;
  return cursiv::kNoRow;
}();
static_assert(identifier != cursiv::kNoRow);
constexpr cursiv::LlAtnState states[] = {
    {cursiv::LlAtnKind::basic, 0, 0, 0, 2},
    {cursiv::LlAtnKind::basic, 0, cursiv::kNoRow, 2, 1},
    {cursiv::LlAtnKind::basic, 0, cursiv::kNoRow, 3, 1},
    {cursiv::LlAtnKind::basic, 0, cursiv::kNoRow, 4, 1},
    {cursiv::LlAtnKind::basic, 0, cursiv::kNoRow, 5, 1},
    {cursiv::LlAtnKind::ruleStop, 0}};
constexpr cursiv::LlAtnEdge edges[] = {
    {cursiv::LlEdgeKind::epsilon, 0, 1},
    {cursiv::LlEdgeKind::epsilon, 0, 2},
    {cursiv::LlEdgeKind::condition, 0, 3},
    {cursiv::LlEdgeKind::condition, 1, 4},
    {cursiv::LlEdgeKind::atom, identifier, 5},
    {cursiv::LlEdgeKind::atom, identifier, 5}};
constexpr std::uint32_t starts[] = {0}, callers[] = {0, 0};
constexpr cursiv::LlTable::Decision decisions[] = {
    {.how = cursiv::LlPredict::adaptive, .mode = base.errMode, .state = 0}};
bool line_condition(const char* text, cursiv::TextOffset len, unsigned k,
                    const cursiv::Token& token, cursiv::TextOffset from, bool,
                    cursiv::TextOffset*) {
  const auto end = std::min(token.begin, len);
  const bool line =
      std::string_view(text + std::min(from, end), end - std::min(from, end))
          .find_first_of("\r\n") != std::string_view::npos;
  return k == 0 ? line : !line;
}
bool end_condition(const char*, cursiv::TextOffset len, unsigned k,
                   const cursiv::Token& token, cursiv::TextOffset, bool atEnd,
                   cursiv::TextOffset*) {
  const bool end = atEnd || cursiv::gap_end(len, token);
  return k == 0 ? end : !end;
}
constexpr auto gapTable = [] {
  auto t = base;
  t.atn = {states, 6, edges, starts, callers, nullptr};
  t.edgeNode = nullptr;
  t.nrules = 1;
  t.decisions = decisions;
  t.ndecisions = 1;
  t.condition = line_condition;
  t.ngapConds = 2;
  return t;
}();
constexpr std::uint16_t sectionEnds[] = {identifier, cursiv::kNoRow};
constexpr auto sectionTable = [] {
  auto t = gapTable;
  t.condition = end_condition;
  t.nsections = 2;
  t.sectionEnd = sectionEnds;
  return t;
}();

// The choice asked at a place of one document, under the caches of the parse.
template <const auto& T>
struct Asking {
  explicit Asking(std::string_view source)
      : text(source),
        tree(text),
        builder(tree),
        context((builder.bind_stream(), text), builder) {}

  std::uint16_t operator()(cursiv::TextOffset at, std::uint8_t section = 0) {
    context.at = at;
    context.triviaAt = at;
    context.laValid = false;
    context.laMode = cursiv::kInvalidSymbol;
    context.section = section;
    cursiv::ll_peek<T>(context, T.decisions[0].mode);
    cursiv::ll_episode_engine<T>().abandon();
    cursiv::ll_episode_cache<T>().plan.clear();
    return cursiv::ll_episode<T>(context, 0, T.decisions[0]);
  }

  std::string_view text;
  cursiv::Cst tree;
  cursiv::CstBuilder builder;
  cursiv::LlContext context;
};
}  // namespace cache_test

void predictions_keep_the_gap_and_embedded_section() {
  using namespace cache_test;
  {
    cursiv::LlPredictionSession<gapTable> session;
    Asking<gapTable> at(" name;\nname;  name; name;\n\nname;");
    CHECK(at(0) == 1);
    CHECK(at(6) == 0);
    CHECK(at(12) == 1);
    CHECK(at(19) == 1);
  }
  {
    cursiv::LlPredictionSession<sectionTable> session;
    Asking<sectionTable> at(" name;");
    CHECK(at(0, 1) == 0);
    CHECK(at(0, 2) == 1);
    CHECK(at(0, 1) == 0);
  }
}

void predicates_and_implied_tokens_match_the_table_engine() {
  const auto ll = tllconditions::generated::Ll::create_parser();
  const auto lr = tllconditions::generated::Lr::create_parser();
  for (const std::string_view text :
       {"", "one;two;", "one\ntwo", "one.two;", "return one;", "return\none;",
        "{one\ntwo}", "one/*\n*/two", "{return}", "one .two;", "one. two;",
        "reserved;", "one two;"}) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.accepted == b.accepted);
    CHECK(a.diagnostics.empty() == b.diagnostics.empty());
  }
  for (const std::string_view text : {"1+2;", "1+\n2;", "1\n+2;"}) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.accepted == b.accepted);
    CHECK(a.diagnostics.empty() == b.diagnostics.empty());
  }
}

void prediction_crosses_only_the_active_trivia() {
  const auto ll = tllconditions::generated::TriviaLl::create_parser();
  for (const std::string_view text :
       {"f = a;", "f a = b;", "f : a;", " f : a;", "f/*x*/=/*y*/a;"}) {
    const auto a = ll->parse(text);
    CHECK(a.accepted && a.diagnostics.empty() && a.value != nullptr);
  }
}

void document_end_elects_default_trivia_before_other_regions() {
  constexpr const auto& table = tllconditions::generated::TriviaLl::kTable;
  const std::string_view text = "f=a; \n/*tail*/\n";
  const auto scan =
      cursiv::ll_match_text<table>(text.data(), text.size(), table.errMode, 4);
  CHECK(scan.term != cursiv::kInvalidSymbol && !table.terms[scan.term].trivia);
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  builder.leaf(0, 4, cursiv::StampLayout{}.make(cursiv::kNoRow, 0));
  cursiv::LlContext context(text, builder);
  context.at = 4;
  CHECK(cursiv::ll_tail<table>(context));
  cursiv::ll_flush_trivia<table>(context);
  CHECK(!context.repaired && context.la.sym == cursiv::kEofSymbol);
  CHECK(context.triviaAt == text.size());
  CHECK(tree.size() == 2 && tree[1].hidden && !tree[1].recovered);
  CHECK(tree[1].begin == 6 && tree[1].end == 14);
}

void deferred_guards_reserve_only_reachable_keywords() {
  const auto ll = tllconditions::generated::TriviaLl::create_parser();
  // `get` is a keyword in another definition. Inside guarded Names it is
  // an identifier, including when the prediction must cross a trivia gap.
  for (const std::string_view text :
       {"get?;", "f get=a;", "f/*gap*/get = a;", "f get:a;"}) {
    const auto a = ll->parse(text);
    CHECK(a.accepted && a.diagnostics.empty() && a.value != nullptr);
  }
  const auto a = ll->parse("f reserved=a;");
  CHECK(!a.diagnostics.empty());
}

void predicates_survive_lowering_and_prevent_static_token_decisions() {
  const auto low = cursiv::LlLowering<tllconditions::Ll>::lower();
  CHECK(!low.cfg.lowerFail() && !low.fault);
  CHECK(low.cfg.ngapConds >= 4);
  bool hasCondition = false, hasImplied = false;
  for (const auto& node : low.cfg.nodes.v) {
    hasCondition = hasCondition || node.kind == cursiv::LlShape::condition;
    hasImplied = hasImplied || node.kind == cursiv::LlShape::implied;
  }
  CHECK(hasCondition && hasImplied);
  CHECK(low.cfg.nclimbOps == 1);
  const auto body = low.cfg.climbOps[0].body;
  const auto& op = low.cfg.nodes[body];
  CHECK(op.kind == cursiv::LlShape::seq && op.count == 2);
  CHECK(low.cfg.nodes[low.cfg.kids[op.sym]].kind == cursiv::LlShape::condition);
  CHECK(low.cfg.atomic_operator(body) != cursiv::kNoRow);
  const auto built = cursiv::ll_build_program(low);
  CHECK(!built.fault && !built.overflow);
  CHECK(built.nadaptive != 0);
}

// A condition past what a table names is refused by name, not wrapped.
void conditions_past_the_table_are_refused() {
  auto low = cursiv::LlLowering<tllconditions::Ll>::lower();
  low.cfg.ngapConds = cursiv::kGapConds;
  const std::uint16_t one[] = {0};
  CHECK(low.cfg.gap_conjoin(one) == cursiv::kNoRow);
  CHECK(low.cfg.lowerWhy == cursiv::LowerFail::gapTooMany);
  CHECK(low.cfg.ngapConds == cursiv::kGapConds);
}

struct Doc : cursiv::Extends<cursiv::AstNode> {};
struct Arrow : cursiv::GrammarSpec {
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>(!(some(lit<"-">) + lit<">">) + ref<X>)> {};
  using entry = Root;
};

void a_condition_that_reads_a_run_without_end_is_warned_of() {
  const auto arrow = cursiv::LlLowering<Arrow>::lower();
  const auto& found = arrow.cfg.unbounded;
  CHECK(found.size() == 1);
  CHECK(cursiv::unbounded_condition_text(
            arrow.cfg, arrow.cfg.rule_name(found[0].rule), found[0])
            .find("rule `Root` asks a condition of the token in hand that "
                  "reads `('-')+` to any length") != std::string::npos);
}

}  // namespace

namespace gap_conditions {

namespace {

bool guarded_head(const char* text, cursiv::TextOffset len, unsigned,
                  const cursiv::Token& token, cursiv::TextOffset, bool,
                  cursiv::TextOffset* seen) {
  if (seen != nullptr)
    *seen =
        token.end > token.begin ? std::min<cursiv::TextOffset>(len, 7) : len;
  const std::string_view input(text, len);
  return input.starts_with('a') && !input.starts_with("abcdef");
}
constexpr cursiv::LlTable headTable{.condition = guarded_head, .ngapConds = 1};

void prediction_keeps_head_guard_reads_beyond_the_candidate() {
  for (const std::string_view text : {"abcdef trailer", "abcdeg trailer", "a"}) {
    cursiv::Cst tree(text);
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    cursiv::LlContext context(text, builder);
    cursiv::LlLookRead dependency;
    const auto answer = cursiv::ll_predict_condition<headTable>(
        context, 0, {0, 1, 1}, 0, dependency);
    CHECK(answer == (text != "abcdef trailer"));
    CHECK(dependency.inspected == std::min<std::size_t>(text.size(), 7));
    CHECK(dependency.ended == (text.size() <= 7));
    dependency = {};
    cursiv::ll_predict_condition<headTable>(context, 0, {0, 0, 1}, 0, dependency);
    CHECK(dependency.inspected == text.size() && dependency.ended);
  }
}

}  // namespace

void check_gap_conditions() {
  prediction_keeps_head_guard_reads_beyond_the_candidate();

}
}

int main() {
  gap_conditions::check_gap_conditions();
  a_condition_that_reads_a_run_without_end_is_warned_of();
  predicates_survive_lowering_and_prevent_static_token_decisions();
  conditions_past_the_table_are_refused();
  predicates_and_implied_tokens_match_the_table_engine();
  prediction_crosses_only_the_active_trivia();
  document_end_elects_default_trivia_before_other_regions();
  predictions_keep_the_gap_and_embedded_section();
  deferred_guards_reserve_only_reachable_keywords();
  return cursiv::test::report("LlConditions_test");
}
