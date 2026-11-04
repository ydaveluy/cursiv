#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/ll/runtime/LlPredictionWork.hpp"
#include "cursiv/ll/runtime/ll_rescan.hpp"
#include "cursiv/ll/runtime/ll_search.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/lr_rescan.hpp"
#include "cursiv/lr/runtime/lr_search.hpp"
#include "cursiv/shared/runtime/TypoCandidates.hpp"
#include "cursiv/AstText.hpp"
#include "cursiv/Check.hpp"
#include "engines/ll/LlTerms.hpp"
#include "engines/ll/ReservedRefusedGlrGrammar.hpp"
#include "engines/ll/ReservedRefusedGrammar.hpp"
#include "engines/ll/ReservedRefusedPeekGrammar.hpp"
#include "rcondll/generated/Ll/Module.hpp"
#include "refusedcondll/generated/CondLl/Module.hpp"
#include "refusedcondll/generated/CondLl/Table.hpp"
#include "refusedll/generated/Ll/AstReflection.hpp"
#include "refusedll/generated/Ll/Grammar.hpp"
#include "refusedll/generated/Ll/Module.hpp"
#include "refusedll/generated/Ll/Parser.hpp"
#include "refusedll/generated/Ll/Table.hpp"
#include "refusedlr/generated/Lr/Module.hpp"
#include "refusedlr/generated/Lr/Table.hpp"
#include "refusedmemoll/generated/MemoLl/Module.hpp"
#include "refusedmemolr/generated/MemoLr/Module.hpp"

namespace reserved_refused_cases {
namespace {

using Cases = std::span<const std::pair<std::string_view, std::string_view>>;
using Spell = std::string (*)(const cursiv::ParseResult&);

std::string plain_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefused::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefused::Item* i : d->items)
    out += i->keyword ? "IN " : i->dash ? "- " : std::string(i->name) + ' ';
  return out;
}

std::string memo_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefused::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefused::Item* i : d->items)
    out += (i->keyword ? "IN " : "") + std::string(i->name) + ' ';
  return out;
}

std::string several_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefusedglr::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefusedglr::Item* i : d->items)
    out += i->keyword ? "IN "
           : i->tag.empty() ? "n:" + std::string(i->name) + ' '
                            : "t:" + std::string(i->tag) + ' ';
  return out;
}

std::string peek_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefusedpeek::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefusedpeek::Item* i : d->items)
    out += i->keyword ? "IN "
           : i->tag.empty() ? "n:" + std::string(i->name) + ' '
                            : "t:" + std::string(i->tag) + ' ';
  return out;
}

void check(const cursiv::Parser& ll, const cursiv::Parser* lr, Spell spell,
           Cases cases, std::string_view prefix = {}) {
  for (const auto& [text, expected] : cases) {
    const std::string source = std::string(prefix) + std::string(text);
    const auto a = ll.parse(source);
    bool same = a.diagnostics.empty() && spell(a) == expected;
    if (lr) {
      const auto b = lr->parse(text);
      same = same && b.diagnostics.empty() && spell(b) == expected &&
          cursiv::test::records(a.cst()) == cursiv::test::records(b.cst());
    }
    if (!same)
      std::printf("`%.*s`: got `%s`, expected `%.*s`\n",
                  static_cast<int>(text.size()), text.data(), spell(a).c_str(),
                  static_cast<int>(expected.size()), expected.data());
    CHECK(same);
  }
}

// The re-readings of the text as the name its start owes: the state, or the
// point, that takes a name and the keyword beside it.
std::vector<cursiv::Typo> lr_names(std::string_view text) {
  namespace lr = tllrefused::generated::Lr;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  cursiv::LrContext c(text, builder);
  std::uint64_t read = 0;
  bool cut = false;
  return cursiv::lr_rescan_candidates<lr::kTable>(
      c, 0, 0, static_cast<cursiv::TextOffset>(text.size()), read, cut);
}

std::vector<cursiv::Typo> ll_names(std::string_view text) {
  namespace ll = tllrefused::generated::Ll;
  constexpr const auto& table = ll::kTable;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  cursiv::LlContext c(text, builder);
  const auto name =
      cursiv::ll_match_text<table>("inx", 3, table.errMode, 0).term;
  std::array<std::uint64_t, table.termWords> want{};
  want[name >> 6] |= std::uint64_t{1} << (name & 63u);
  // The whole text is within reach of the walk.
  const auto len = static_cast<cursiv::TextOffset>(text.size());
  const cursiv::TextOffset plain =
      cursiv::ll_match_text<table>(c.src, len, table.errMode, 0).end;
  std::uint64_t read = 0;
  bool cut = false;
  cursiv::TypoCandidates found;
  cursiv::ll_rescan_within<table>(c, table.errMode, want.data(), 0, plain, len,
                                  len, read, found, ~std::uint64_t{0}, cut);
  return found.ranked();
}

// A keyword stands, and its spelling is no name for a repair to join to what
// follows; refused where the word it refuses is spelled, it reserves nothing.
void a_refused_keyword_reserves_nothing_from_a_misspelling() {
  namespace lr = tllrefused::generated::Lr;
  const auto name =
      cursiv::lr_match_text<lr::kTable>("inx", 3, lr::kTable.mode[0], 0).term;
  const auto len = [](std::string_view text) {
    return static_cast<cursiv::TextOffset>(text.size());
  };
  const auto whole = [&](std::string_view text) {
    for (const cursiv::Typo& t : lr_names(text))
      if (t.term == name && t.end == len(text) &&
          t.cost == cursiv::kRepairReread)
        return true;
    return false;
  };
  CHECK(whole("in-place"));
  CHECK(whole("inx-place"));
  CHECK(!whole("in place"));
  // A search's bound reads as the search does: the keyword's refusal is told at
  // its token, wherever that stands in the text.
  {
    const std::string_view text = "x in-place";
    cursiv::Cst cst(text);
    cursiv::CstBuilder builder(cst);
    cursiv::LrContext c(text, builder);
    cursiv::LrSearch s(c.len, cursiv::RepairBudget{}, c.indentArena);
    cursiv::lr_search_reset(s);
    bool found = false;
    for (const cursiv::Typo& t : cursiv::lr_search_misread<lr::kTable>(c, s, 2))
      found = found || (t.term == name && t.end == len(text));
    CHECK(found);
  }
  namespace ll = tllrefused::generated::Ll;
  const auto llName =
      cursiv::ll_match_text<ll::kTable>("inx", 3, ll::kTable.errMode, 0).term;
  const auto ll_whole = [&](std::string_view text) {
    for (const cursiv::Typo& t : ll_names(text))
      if (t.term == llName && t.end == len(text) &&
          t.cost == cursiv::kRepairReread)
        return true;
    return false;
  };
  CHECK(ll_whole("in-place"));
  CHECK(ll_whole("inx-place"));
  CHECK(!ll_whole("in place"));
}

// A refused word spelled to the end of what a walk reads ran into that end:
// the byte that rescues it may stand past a cut there, so the walk is cut.
void a_word_refused_at_a_cut_runs_into_it() {
  constexpr std::string_view text = "in-placex";
  constexpr auto cut = static_cast<cursiv::TextOffset>(text.size() - 1);
  namespace lr = tllrefused::generated::Lr;
  constexpr auto lrMode = lr::kTable.errMode;
  const auto lrWhole =
      cursiv::lr_match_text<lr::kTable>(text.data(), cut + 1, lrMode, 0);
  cursiv::TextOffset halt = 0;
  const auto lrCut =
      cursiv::lr_match_halt<lr::kTable>(text.data(), cut, lrMode, 0, halt);
  CHECK(halt == cut && lrCut.term != lrWhole.term);
  namespace ll = tllrefused::generated::Ll;
  constexpr const auto& table = ll::kTable;
  constexpr auto mode = table.errMode;
  const auto llWhole =
      cursiv::ll_match_text<table>(text.data(), cut + 1, mode, 0);
  cursiv::TextOffset reached = 0;
  const auto llCut =
      cursiv::ll_match_text<table>(text.data(), cut, mode, 0, reached);
  CHECK(reached == cut && llCut.term != llWhole.term);
  // A search whose share ends inside a token keeps nothing of it.
  const std::string word(2 * cursiv::kRepairLexBytes, 'x');
  cursiv::Cst tree(word);
  cursiv::CstBuilder builder(tree);
  cursiv::LlContext c(word, builder);
  cursiv::LlSearch s(table.atn, c.len);
  s.limit = 1;
  auto& row = cursiv::ll_search_row<table>(s, 0);
  CHECK(cursiv::ll_search_scan<table>(c, s, row, mode, 0).term ==
        cursiv::kInvalidSymbol);
  CHECK(row[mode].end == cursiv::kLlUnscanned && cursiv::ll_search_spent(s));
}

// A keyword a condition's name is checked against reserves the name's text
// unless the word it refuses is spelled behind it: the check reads that word,
// and to the end of the text where the word runs into it.
void a_reservation_reads_the_word_its_keyword_refuses() {
  namespace cl = tllrefused::generated::CondLl;
  constexpr const auto& table = cl::kTable;
  constexpr cursiv::SymbolId name = ll_terms::named<table>("NAME");
  static_assert(name != cursiv::kInvalidSymbol);
  constexpr std::pair<std::string_view, bool> cases[] = {
      {"in-place", false}, {"in-pla", true}, {"in-placex", true}};
  for (const auto& [text, reserved] : cases) {
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    cursiv::TextOffset seen = 0;
    CHECK(cl::predicate_reserved(text.data(), len, 0, 2, table.errMode, name,
                                 &seen) == reserved);
    CHECK(seen == len);
  }
}

}  // namespace

// A keyword refused at a position is off it and reserves nothing there: the
// bytes read as a name, in either engine, and in a mode that reads several
// candidates.
// A refused keyword takes itself out of the election, and only itself.
void refused_keyword_cases(const char* engine, const cursiv::Parser& parser) {
  std::string far = "rivals: X";
  for (unsigned i = 0; i < 100; ++i) far += " a";
  for (const std::string& text :
       {std::string{"rivals:X q"}, std::string{"rivals: X b"},
        std::string{"rivals: X c"}, std::string{"rivals: X a b"},
        std::string{"rivals: X a c"}, far + " b", far + " c"}) {
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    if (!result.diagnostics.empty())
      std::printf("%s: %.20s: %zu diagnostics\n", engine, text.c_str(),
                  result.diagnostics.size());
  }
}

void a_refused_keyword_leaves_the_terminals_behind_it() {
  refused_keyword_cases("LL", *tllrefused::generated::Ll::create_parser());
  refused_keyword_cases("LR", *tllrefused::generated::Lr::create_parser());
}

bool parse_clean(const cursiv::Parser& parser, std::string_view text) {
  const auto result = parser.parse(text);
  return result.accepted && result.diagnostics.empty();
}

std::vector<std::string> offered(const cursiv::Parser& parser,
                                 const std::string& text,
                                 cursiv::TextOffset at) {
  const cursiv::ParseResult parsed = parser.parse(text);
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(parsed.cst(), at, at, next);
  std::vector<std::string> out;
  for (const cursiv::NextFeature& f : next) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    out.emplace_back(spellings.size() == 1
                         ? spellings.front()
                         : parser.getGrammar().terminalName(f.feature));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// Completion reads what the parse reads: each terminal that ties behind a
// refused keyword goes on, on a whole document as on a cut one.
void completion_keeps_the_terminals_behind_a_refused_keyword() {
  const auto ll = tllrefused::generated::Ll::create_parser();
  const auto lr = tllrefused::generated::Lr::create_parser();
  const std::vector<std::string> next{"a", "b", "c"};
  std::string far = "rivals: X";
  for (unsigned i = 0; i < 100; ++i) far += " a";
  for (const std::string& written :
       {std::string{"rivals: X "}, std::string{"rivals: X a "}, far + " "})
    for (const char* rest : {"", "b", "c"}) {
      const std::string text = written + rest;
      const auto at = static_cast<cursiv::TextOffset>(written.size());
      CHECK(offered(*ll, text, at) == next);
      CHECK(offered(*lr, text, at) == next);
    }
  // Past the terminal the first reading would not have taken, the replay still
  // stands: the next items, and a caret inside one of them.
  const std::vector<std::string> item{"-",      "IN",    "NAME",
                                      "cell:",  "glued:", "note:",
                                      "rivals:"};
  for (const std::string& text :
       {std::string{"rivals: X c"}, std::string{"rivals: X a c"}, far + " c"}) {
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    CHECK(offered(*ll, text, at) == item);
    CHECK(offered(*lr, text, at) == item);
    CHECK(offered(*ll, text + " rivals: X a b", at + 1) == item);
    CHECK(offered(*lr, text + " rivals: X a b", at + 1) == item);
  }
  // Where no gap refuses it the keyword stands and reserves its spelling; where
  // one does, the terminal the same gap guards is refused with it.
  const std::vector<std::string> tight{"q"};
  for (const char* rest : {"", "q"}) {
    const std::string text = std::string{"rivals:X "} + rest;
    CHECK(offered(*ll, text, 9) == tight);
    CHECK(offered(*lr, text, 9) == tight);
  }
  CHECK(!parse_clean(*ll, "rivals:X z") && !parse_clean(*lr, "rivals:X z"));
  CHECK(!parse_clean(*ll, "rivals: X z") && !parse_clean(*lr, "rivals: X z"));
  // A caret glued behind the word: the reading it follows may be either tie.
  const std::vector<std::string> marks{"!", "?"};
  for (const char* text : {"glued: W", "glued: W!", "glued: W?"}) {
    CHECK(offered(*ll, text, 8) == marks);
    CHECK(offered(*lr, text, 8) == marks);
  }
  // Two readings refuse the keyword from one cell.
  for (const char* text : {"cell: V !", "cell: V ?"}) {
    CHECK(parse_clean(*ll, text) && parse_clean(*lr, text));
    CHECK(offered(*ll, text, 8) == marks);
    CHECK(offered(*lr, text, 8) == marks);
  }
  // A keyword refused where a trivia ties leaves the position to the trivia,
  // and completion reads on past it.
  CHECK(parse_clean(*lr, "note: N k m"));
  CHECK(offered(*lr, "note: N k m", 10) == std::vector<std::string>{"m"});
  CHECK(parse_clean(*ll, "note: k m") && parse_clean(*lr, "note: k m"));
  // What a run with ties answered is no answer to a run without.
  for (const char* text : {"rivals: Xa b rivals: X a c",
                           "rivals: X a c rivals: Xa b"})
    CHECK(parse_clean(*ll, text) && parse_clean(*lr, text));
  CHECK(parse_clean(*ll, "note:N q") && parse_clean(*lr, "note:N q"));
  // Both engines read every caret alike.
  for (const std::string text :
       {"rivals: X b", "rivals: X c", "rivals: X a b", "rivals: X a c",
        "rivals:X q", "rivals:X z", "x rivals: X a c",
        "rivals: X a c rivals: X a b", "rivals:  X  a  c", "rivals: X",
        "rivals: X a", "glued: W", "glued: W!", "glued: W?", "glued:W q",
        "cell: V !", "cell: V ?", "cell:V q", "cell:V r", "cell:e1V q",
        "cell:e2V r", "note: k m", "note:N q"})
    for (cursiv::TextOffset at = 0; at <= text.size(); ++at) {
      const auto a = offered(*ll, text, at), b = offered(*lr, text, at);
      CHECK(a == b);
      if (a != b)
        std::printf("caret differs: [%s|%s]\n", text.substr(0, at).c_str(),
                    text.substr(at).c_str());
    }
}

cursiv::CancellationTokenSource* stopOnEntry = nullptr;
void stopping_drive(cursiv::LlContext& c) {
  if (stopOnEntry != nullptr) stopOnEntry->request_stop();
  tllrefused::generated::Ll::kDrive(c);
}

// A stop during the reading a refusal asks for leaves nothing of that reading
// to the next parse on the thread.
void a_stop_in_a_refused_reading_leaves_nothing_behind() {
  namespace ll = tllrefused::generated::Ll;
  const cursiv::LlLanguageParser<ll::kTable> parser(
      ll::kGrammarMetaData, ll::kAstActions, stopping_drive);
  std::string far = "rivals: X";
  for (unsigned i = 0; i < 2000; ++i) far += " a";
  far += " c";
  cursiv::CancellationTokenSource source;
  stopOnEntry = &source;
  bool threw = false;
  try {
    (void)parser.parse(far, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  stopOnEntry = nullptr;
  CHECK(threw);
  const auto read = [&] {
    const std::uint64_t before = cursiv::LlPredictionWork::current().tokens;
    const auto after = parser.parse(far);
    CHECK(after.accepted && after.diagnostics.empty());
    return cursiv::LlPredictionWork::current().tokens - before;
  };
  const std::uint64_t first = read();
  CHECK(first == read());
}

void run() {
  a_refused_keyword_leaves_the_terminals_behind_it();
  completion_keeps_the_terminals_behind_a_refused_keyword();
  a_stop_in_a_refused_reading_leaves_nothing_behind();
  a_refused_keyword_reserves_nothing_from_a_misspelling();
  a_word_refused_at_a_cut_runs_into_it();
  a_reservation_reads_the_word_its_keyword_refuses();
  constexpr std::pair<std::string_view, std::string_view> plain[] = {
      {"in x", "IN x "},
      {"in", "IN "},
      {"inx", "inx "},
      {"in place", "IN place "},
      {"in -place", "IN - place "},
      {"in-place", "in - place "},
      {"in-placex", "IN - placex "},
      {"x in-place in", "x in - place IN "},
  };
  check(*tllrefused::generated::Ll::create_parser(),
        tllrefused::generated::Lr::create_parser().get(), plain_items, plain);
  // Each `(())` follows a blank, so the second statement is predicted from
  // what the first remembered: the refused word read past `in` is part of
  // what that memory compares.
  constexpr std::pair<std::string_view, std::string_view> memo[] = {
      {"() in-plaze.", "IN plaze "},
      {"() in-place.", "in "},
      {" (()) in-plaze. (()) in-place.", "IN plaze in "},
      {" (()) in-place. (()) in-plaze.", "in IN plaze "},
  };
  check(*tllrefused::generated::MemoLl::create_parser(),
        tllrefused::generated::MemoLr::create_parser().get(), memo_items, memo);
  // A dash stands before a name, which the keyword refused there leaves.
  constexpr std::pair<std::string_view, std::string_view> cond[] = {
      {"in-place", "in - place "},
      {"in-placex", "IN - placex "},
      {"- in-place", "- in - place "},
  };
  check(*tllrefused::generated::CondLl::create_parser(), nullptr, plain_items,
        cond);
  constexpr std::pair<std::string_view, std::string_view> peek[] = {
      {"in place.", "IN n:place "},
      {"in-place.", "t:in n:place "},
      {"in-place.in-place.", "t:in n:place t:in n:place "},
      {"inx-", "t:inx "},
      {"x- in-place.", "t:x t:in n:place "},
  };
  check(*tllguardcases::generated::Ll::create_parser(),
        nullptr, peek_items, peek, "peek:");
  constexpr std::pair<std::string_view, std::string_view> several[] = {
      {"in place.", "IN n:place "},
      {"in-place.", "t:in n:place "},
      {"in-place.in-place.", "t:in n:place t:in n:place "},
      {"inx.", "n:inx "},
      {"inx-", "t:inx "},
      {"x- in-place.", "t:x t:in n:place "},
  };
  check(*tllguardcases::generated::Ll::create_parser(),
        nullptr, several_items, several, "several:");
}

}  // namespace reserved_refused_cases
