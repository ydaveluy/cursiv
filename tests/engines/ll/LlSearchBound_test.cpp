// A clipped lexical walk answers nothing. A later search with more room must
// still read the complete token, predicate or section.
#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/engines/ll/runtime/ll_search.hpp"
#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/LlTerms.hpp"
#include "llindent/generated/Ll/Table.hpp"
#include "parityll/generated/Ll/Table.hpp"
#include "predicatesll/generated/Ll/Table.hpp"
#include "sectiontailll/generated/Ll/Table.hpp"

namespace {

constexpr cursiv::RepairBudget kTiny{
    .floor = 1, .perByte = 0, .perSearch = 1, .perParse = 1};
constexpr const auto& kPlain = tpar::generated::Ll::kTable;
constexpr const auto& kPredicates = tpredicates::generated::Ll::kTable;
cursiv::TextOffset conditionLength = 0;

bool observed_condition(const char* src, cursiv::TextOffset len, unsigned id,
                        const cursiv::Token& token, cursiv::TextOffset from,
                        bool atEnd, cursiv::TextOffset* seen) {
  conditionLength = len;
  return kPredicates.condition(src, len, id, token, from, atEnd, seen);
}

constexpr auto kObserved = [] {
  auto t = kPredicates;
  t.condition = observed_condition;
  return t;
}();
constexpr auto kIdentifier = ll_terms::named<kPredicates>("ID");
static_assert(kIdentifier != cursiv::kInvalidSymbol);

void a_clipped_token_is_not_memoized() {
  const std::string text = std::string(131072, 'a') + ';';
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  cursiv::LlContext c(text, builder, kTiny);
  cursiv::LlSearch s(kPlain.atn, c.len, kTiny);
  s.limit = 1;
  const auto mode = kPlain.errMode;
  auto& row = cursiv::ll_search_row<kPlain>(s, 0);
  const auto clipped = cursiv::ll_search_scan<kPlain>(c, s, row, mode, 0);
  CHECK(clipped.term == cursiv::kInvalidSymbol);
  CHECK(row[mode].end == cursiv::kLlUnscanned);
  CHECK(cursiv::ll_search_spent(s) && s.work <= 3);
  s.work = 0;
  s.limit = 100000;
  const auto whole = cursiv::ll_search_scan<kPlain>(c, s, row, mode, 0);
  CHECK(whole.term != cursiv::kInvalidSymbol && whole.end == text.size() - 1);
  CHECK(row[mode].end == whole.end && !cursiv::ll_search_spent(s));
  cursiv::LlConfig cfg;
  for (std::uint32_t q = 0; q < kPlain.atn.nstates; ++q)
    for (const auto& atom : cursiv::ll_atoms<kPlain>().at(q))
      if (atom.label == whole.term) cfg.state = q;
  s.work = 0;
  s.limit = 1;
  std::vector<std::uint16_t> terms;
  cursiv::ll_search_terms<kPlain>(
      c, s, &cfg, 1, cursiv::Token{0, whole.end, whole.term}, terms);
  CHECK(terms.empty() && cursiv::ll_search_spent(s) && s.work <= 3);
}

void a_guard_reads_only_its_share_of_a_long_gap() {
  const std::string text = "a" + std::string(131072, ' ') + "b";
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  cursiv::LlContext c(text, builder, kTiny);
  cursiv::LlSearch s(kObserved.atn, c.len, kTiny);
  s.limit = 1;
  // The fixture's first condition reads Split: ID followed by literal 'b'.
  // Its clipped view must also bypass the full-document suffix cache.
  cursiv::LexSuffixSession<cursiv::LlLexTape<kPredicates>> session(c.src, c.len);
  const cursiv::Token token{0, 1, kIdentifier};
  CHECK(!cursiv::ll_search_condition<kObserved>(c, s, 0, token, 0));
  CHECK(conditionLength == cursiv::kRepairLexBytes);
  CHECK(cursiv::ll_search_spent(s) && s.work <= 3);
  s.work = 0;
  s.limit = 100000;
  CHECK(cursiv::ll_search_condition<kObserved>(c, s, 0, token, 0));
  CHECK(conditionLength == text.size() && !cursiv::ll_search_spent(s));
  cursiv::TextOffset seen = 0;
  CHECK(cursiv::ll_condition_text<kPredicates>(text, 0, token, 0, false, &seen));
  CHECK(seen == text.size());
}

void an_artificial_end_is_no_dedent_or_section_end() {
  {
    constexpr const auto& table = tllindent::generated::Ll::kTable;
    const std::string text = std::string(131072, ' ') + "let";
    cursiv::Cst tree(text);
    cursiv::CstBuilder builder(tree);
    cursiv::LlContext c(text, builder, kTiny);
    cursiv::LlSearch s(table.atn, c.len, kTiny);
    s.limit = 1;
    cursiv::LlReading r;
    r.ahead.head = true;
    CHECK(cursiv::ll_search_peek<table>(c, s, r).sym == cursiv::kInvalidSymbol);
    CHECK(s.lexed.empty() && cursiv::ll_search_spent(s) && s.work <= 3);
  }
  {
    constexpr const auto& table = tsection::generated::Ll::kTable;
    const std::string text = "<" + std::string(131072, 'a') + ">";
    cursiv::Cst tree(text);
    cursiv::CstBuilder builder(tree);
    cursiv::LlContext c(text, builder, kTiny);
    c.section = 1;
    cursiv::LlSearch s(table.atn, c.len, kTiny);
    s.limit = 1;
    CHECK(cursiv::ll_search_reading<table>(c, s, 1).term ==
          cursiv::kInvalidSymbol);
    CHECK(s.readings.empty() && cursiv::ll_search_spent(s) && s.work <= 3);
  }
}

// A fingerprint selects the readings that may share a future; their futures
// decide. A reading whose fingerprint a cheaper one holds, as a collision
// would have it, still stands where its positions differ, and one whose
// positions are the same dies to it.
void a_shared_fingerprint_drops_only_a_shared_future() {
  cursiv::LlSearch s(kPlain.atn, 0);
  s.pool = {cursiv::LlConfig{0, cursiv::kLlCtxNone, 0},
            cursiv::LlConfig{1, cursiv::kLlCtxNone, 0}};
  cursiv::LlReading cheap;
  cheap.ncfg = 1;
  cheap.queued = true;
  cursiv::LlReading other = cheap;
  other.cfg = 1;
  other.cost = 4;
  other.queued = false;
  cursiv::LlReading twin = cheap;
  twin.cost = 4;
  twin.queued = false;
  s.nodes = {cheap, other, twin};
  s.seen.emplace(cursiv::ll_search_future<kPlain>(s, s.nodes[1]), 0u);
  cursiv::ll_search_admit<kPlain>(s, 1);
  CHECK(!s.nodes[1].gone && s.nodes[1].queued && !s.nodes[0].gone);
  s.seen.emplace(cursiv::ll_search_future<kPlain>(s, s.nodes[0]), 0u);
  cursiv::ll_search_admit<kPlain>(s, 2);
  CHECK(s.nodes[2].gone && !s.nodes[2].queued && !s.nodes[0].gone);
}

}  // namespace

int main() {
  a_clipped_token_is_not_memoized();
  a_guard_reads_only_its_share_of_a_long_gap();
  an_artificial_end_is_no_dedent_or_section_end();
  a_shared_fingerprint_drops_only_a_shared_future();
  return cursiv::test::report("LlSearchBound_test");
}
