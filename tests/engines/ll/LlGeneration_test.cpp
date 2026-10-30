#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/LlRecordPlan.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/gen/ll_emit_parser.hpp"
#include "cursiv/engines/ll/gen/ll_emit_table.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/InfixLevel.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/CaretAgreementGrammar.hpp"
#include "engines/ll/IndirectFoldGrammar.hpp"
#include "engines/ll/LexicalPredicatesGrammar.hpp"
#include "engines/ll/StagedGuardGrammar.hpp"
#include "engines/shared/GuardedClimbGrammar.hpp"

namespace {

void record_sources_keep_nullable_suffixes_and_only_required_children() {
  constexpr unsigned count = 8000, middle = count / 2;
  cursiv::LlLowCfg cfg;
  cfg.nrules = 2;
  cfg.nnodes = 2 * count + 1;
  cfg.nkids = count;
  std::vector<std::uint8_t> nullable(cfg.nnodes, 1);
  for (unsigned i = 0; i < count; ++i) {
    cfg.nodes[2 * i] = {.kind = cursiv::LlShape::term,
                        .sym = 1,
                        .row = static_cast<std::uint16_t>(i < middle    ? 0
                                                          : i == middle ? 2
                                                                        : 1)};
    nullable[2 * i] = 0;
    cfg.nodes[2 * i + 1] = {.kind = cursiv::LlShape::rep,
                            .sym = static_cast<std::uint16_t>(2 * i),
                            .min = 0,
                            .max = 1};
    cfg.nodeBuild[2 * i] = {};
    cfg.nodeBuild[2 * i + 1] = {};
    cfg.kids[i] = static_cast<std::uint16_t>(2 * i + 1);
  }
  cfg.nodes[2 * count] = {
      .kind = cursiv::LlShape::seq, .sym = 0, .count = count};
  cfg.nodeBuild[2 * count] = {};
  cfg.rules[1].body = 2 * count;
  cursiv::ChainPool writes;
  for (unsigned i = 0; i < 4; ++i) writes.chainBuild[i] = {};
  const std::vector<std::uint8_t> nests(4, 0);
  cursiv::LlRecordPlan records(cfg, writes, nests);
  const auto join = [](std::uint16_t held, std::uint16_t) { return held; };
  records.settle_rows(nullable, join);
  CHECK((records.rule_rows(1) == std::vector<std::uint16_t>{0, 2, 1}));
  // A required position, as the lowering spells a count of one: a sequence of
  // one copy of the body.
  cfg.kids[count] = static_cast<std::uint16_t>(2 * middle);
  cfg.nkids = count + 1;
  cfg.nodes[2 * middle + 1] = {
      .kind = cursiv::LlShape::seq, .sym = count, .count = 1};
  nullable[2 * middle + 1] = nullable.back() = 0;
  records.settle_rows(nullable, join);
  CHECK((records.rule_rows(1) == std::vector<std::uint16_t>{2, 1}));
  cfg.nodes[2 * count].owns = true;
  cfg.nodes[2 * count].row = 3;
  records.settle_rows(nullable, join);
  std::vector<std::uint16_t> offered;
  records.own_joins(2 * count, join, [&](const cursiv::LlFuse& x) {
    offered.push_back(x.from);
  });
  CHECK((offered == std::vector<std::uint16_t>{2}));

  // An owning wrapper can look through thousands of transparent groups.
  cursiv::LlLowCfg deep;
  deep.nrules = 2;
  deep.nnodes = count;
  deep.nkids = count - 1;
  deep.nodes[0] = {.kind = cursiv::LlShape::term, .sym = 1, .row = 2};
  deep.nodeBuild[0] = {};
  for (unsigned i = 1; i < count; ++i) {
    deep.nodes[i] = {.kind = cursiv::LlShape::seq,
                     .sym = static_cast<std::uint16_t>(i - 1),
                     .count = 1};
    deep.nodeBuild[i] = {};
    deep.kids[i - 1] = static_cast<std::uint16_t>(i - 1);
  }
  deep.nodes[count - 1].owns = true;
  deep.nodes[count - 1].row = 3;
  deep.rules[1].body = count - 1;
  cursiv::LlRecordPlan nested(deep, writes, nests);
  nullable.assign(count, 0);
  nested.settle_rows(nullable, join);
  CHECK((nested.rule_rows(1) == std::vector<std::uint16_t>{3, 2}));
}

void record_aliases_propagate_changes_without_rejoining_every_rule() {
  constexpr unsigned count = 8000;
  cursiv::LlLowCfg cfg;
  cfg.nrules = count + 1;
  cfg.nnodes = count;
  for (unsigned r = 1; r <= count; ++r) {
    cfg.rules[r].body = static_cast<std::uint16_t>(r - 1);
    cfg.nodeBuild[r - 1] = {};
    cfg.nodes[r - 1] =
        r < count
            ? cursiv::LlNode{.kind = cursiv::LlShape::call,
                             .sym = static_cast<std::uint16_t>(r + 1),
                             .row = 1}
            : cursiv::LlNode{.kind = cursiv::LlShape::term, .sym = 1, .row = 0};
  }
  cursiv::ChainPool writes;
  writes.chainBuild[0] = {.makes = true, .creates = true};
  writes.chainBuild[1] = {};
  const std::vector<std::uint8_t> nests(2), nullable(count);
  cursiv::LlRecordPlan records(cfg, writes, nests);
  records.settle_ownership();
  std::size_t joins = 0;
  records.settle_rows(nullable, [&](std::uint16_t held, std::uint16_t over) {
    CHECK(over == 1);
    ++joins;
    return held;
  });
  CHECK(joins == count - 1);
  for (std::uint16_t r = 1; r <= count; ++r) {
    CHECK(records.rule_hands(r) && records.rule_opens(r));
    CHECK((records.rule_rows(r) == std::vector<std::uint16_t>{0}));
    cfg.nodes[r - 1].row = cursiv::kNoRow;
  }
  // The same dependency chain also propagates the loss of a guarantee.
  cfg.nodes[count - 1] = {.kind = cursiv::LlShape::empty};
  records.settle_ownership();
  for (std::uint16_t r = 1; r <= count; ++r)
    CHECK(!records.rule_hands(r) && !records.rule_opens(r));
  // A cycle alone supplies no finite opening or action chain.
  cfg.nodes[count - 1] = {.kind = cursiv::LlShape::call, .sym = 1};
  records.settle_ownership();
  records.settle_rows(nullable, [](auto held, auto) { return held; });
  for (std::uint16_t r = 1; r <= count; ++r) {
    CHECK(records.rule_hands(r) && !records.rule_opens(r));
    CHECK(records.rule_rows(r).empty());
  }
}

void record_rows_keep_the_order_of_first_arrival_through_aliases() {
  cursiv::LlLowCfg cfg;
  cfg.nrules = 4;
  cfg.nnodes = 5;
  cfg.nkids = 2;
  cfg.nodes[0] = {.kind = cursiv::LlShape::call, .sym = 2};
  cfg.nodes[1] = {.kind = cursiv::LlShape::term, .sym = 1, .row = 1};
  cfg.nodes[2] = {.kind = cursiv::LlShape::alt, .sym = 0, .count = 2};
  cfg.nodes[3] = {.kind = cursiv::LlShape::call, .sym = 3};
  cfg.nodes[4] = {.kind = cursiv::LlShape::term, .sym = 1, .row = 0};
  for (unsigned id = 0; id < cfg.nnodes; ++id) cfg.nodeBuild[id] = {};
  cfg.kids[0] = 0;
  cfg.kids[1] = 1;
  cfg.rules[1].body = 2;
  cfg.rules[2].body = 3;
  cfg.rules[3].body = 4;
  cursiv::ChainPool writes;
  const std::vector<std::uint8_t> nests, nullable(cfg.nnodes);
  cursiv::LlRecordPlan records(cfg, writes, nests);
  records.settle_rows(nullable, [](auto held, auto) { return held; });
  CHECK((records.rule_rows(1) == std::vector<std::uint16_t>{1, 0}));
  // Once a cycle acquires an exit, it reaches every member without adding
  // duplicates or changing the order of rows that have already arrived.
  cfg.nodes[4] = {.kind = cursiv::LlShape::call, .sym = 1};
  records.settle_rows(nullable, [](auto held, auto) { return held; });
  for (std::uint16_t r = 1; r < cfg.nrules; ++r)
    CHECK((records.rule_rows(r) == std::vector<std::uint16_t>{1}));
}

void only_members_of_a_recursive_component_are_named() {
  cursiv::LlLowered low;
  low.cfg.nrules = 4;
  low.cfg.nnodes = 3;
  low.cfg.entryRule = 1;
  for (std::uint16_t r = 1; r < 4; ++r)
    low.cfg.rules[r].body = r - 1;
  low.cfg.nodes[0] = {.kind = cursiv::LlShape::call, .sym = 2};
  low.cfg.nodes[1] = {.kind = cursiv::LlShape::call, .sym = 3};
  low.cfg.nodes[2] = {.kind = cursiv::LlShape::call, .sym = 2};
  const auto built = cursiv::ll_build_program(low);
  CHECK(built.fault.code == cursiv::GrammarFaultCode::leftRecursion);
  CHECK(built.fault.rule == 2);
  CHECK((built.faultCycle == std::vector<std::uint32_t>{2, 3}));
}

void exploration_reports_a_limit_instead_of_an_incomplete_answer() {
  const cursiv::LlAtnState states[] = {
      {.edge = 0, .nedges = 1}, {.edge = 1, .nedges = 1},
      {.edge = 2, .nedges = 1}, {.edge = 3, .nedges = 1}, {}};
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 1},
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 2},
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 1, .target = 4}};
  const cursiv::LlAtnView atn{.states = states, .nstates = 5, .edges = edges};
  cursiv::LlSimulation limited(atn, 2);
  limited.seed(0, cursiv::kLlCtxNone);
  CHECK(limited.capped());
  cursiv::LlSimulation whole(atn);
  whole.seed(0, cursiv::kLlCtxNone);
  CHECK(!whole.capped() && whole.step(1));
}

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Simple : cursiv::GrammarSpec {
  struct Root : rule<create<Node>((lit<"a"> + lit<"b">) |
                                 (lit<"a"> + lit<"c">))> {};
  using entry = Root;
};

struct UnsupportedGap : cursiv::GrammarSpec {
  struct Root
      : rule<create<Node>(gap::has(&(lit<"a"> + lit<"b">)) + lit<"ok">)> {};
  using entry = Root;
};
struct UnsupportedInnerGap : cursiv::GrammarSpec {
  struct Root
      : rule<create<Node>(&(lit<"a"> + gap::has(&(lit<"a"> + lit<"b">))) +
                          lit<"ok">)> {};
  using entry = Root;
};

void unreadable_gap_operands_are_refused_during_lowering() {
  CHECK(cursiv::LlLowering<UnsupportedGap>::lower().cfg.lowerWhy ==
        cursiv::LowerFail::gapShape);
  CHECK(cursiv::LrLowering<UnsupportedGap>::table().cfg.lowerWhy ==
        cursiv::LowerFail::gapShape);
  CHECK(cursiv::LlLowering<UnsupportedInnerGap>::lower().cfg.lowerWhy ==
        cursiv::LowerFail::gapShape);
  CHECK(cursiv::LrLowering<UnsupportedInnerGap>::table().cfg.lowerWhy ==
        cursiv::LowerFail::gapShape);
}

struct RecursiveAdoption : cursiv::GrammarSpec {
  struct Value;
  struct Atom : rule<create<Node>(lit<"a">)> {};
  struct Value : rule<ref<Atom> | (lit<"("> + ref<Value> + lit<")">)> {};
  using entry = Value;
};

struct LocalGuardChoices : cursiv::GrammarSpec {
  struct Unrelated : rule<lit<"a"> | lit<"b">> {};
  struct AfterToken : rule<(lit<"c"> + gap::line) |
                           (lit<"d"> + !gap::line)> {};
  struct Head : rule<(gap::line + lit<"e">) |
                     (!gap::line + lit<"f">)> {};
  struct Empty : rule<option(lit<"?">)> {};
  struct ReturnGuard : rule<ref<Empty> + gap::line + lit<"g">> {};
  struct Root : rule<create<Node>(
      (lit<"u"> + ref<Unrelated>) | (lit<"t"> + ref<AfterToken>) |
      (lit<"h"> + ref<Head>) | (lit<"n"> + ref<ReturnGuard>))> {};
  using entry = Root;
};

void a_guard_only_disables_the_static_decisions_that_can_reach_it() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<LocalGuardChoices>::lower()),
      "local_guard_test");
  CHECK(done.has_value());
  if (!done) return;
  const auto& built = done->program();
  CHECK(built.cfg.ngapConds != 0);
  const auto choice = [&](std::string_view name) {
    for (const auto& dec : built.cfg.decisions.v)
      if (built.cfg.rule_name(dec.rule) == name) return dec.how;
    CHECK(false);
    return cursiv::LlPredict::staged;
  };
  CHECK(choice("Unrelated") == cursiv::LlPredict::single);
  CHECK(choice("AfterToken") == cursiv::LlPredict::single);
  CHECK(choice("Head") == cursiv::LlPredict::adaptive);
  // A nullable callee reaches the caller's guard before reading a token.
  CHECK(choice("Empty") == cursiv::LlPredict::adaptive);
  const auto files = cursiv::ll_emit_parser_files(
      *done, "local_guard_test",
      cursiv::ll_emit_table(*done, "local_guard_test"));
  std::string emitted;
  for (const auto& [name, text] : files) emitted += text;
  for (std::uint16_t i = 0; i < built.cfg.ndecisions; ++i) {
    const auto& dec = built.cfg.decisions[i];
    if (built.cfg.rule_name(dec.rule) != "Unrelated") continue;
    CHECK(emitted.find("ll_predict<kTable>(c, " + std::to_string(i) + "u)") ==
          std::string::npos);
  }
}

void classification_stops_before_a_guard_can_choose_an_arm() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<tllstaged::Base>::lower()),
      "staged_generation_test");
  CHECK(done.has_value());
  if (!done) return;
  const auto& built = done->program();
  CHECK(built.cfg.ngapConds != 0);
  const auto choice = [&](std::string_view name) {
    for (const auto& dec : built.cfg.decisions.v)
      if (built.cfg.rule_name(dec.rule) == name) return dec.how;
    CHECK(false);
    return cursiv::LlPredict::single;
  };
  CHECK(choice("Unrelated") == cursiv::LlPredict::staged);
  CHECK(choice("AfterSplit") == cursiv::LlPredict::staged);
  CHECK(choice("BeforeSplit") == cursiv::LlPredict::adaptive);
  CHECK(choice("Head") == cursiv::LlPredict::adaptive);
  CHECK(choice("Empty") == cursiv::LlPredict::adaptive);
  const auto table = cursiv::ll_emit_table(*done, "staged_generation_test");
  CHECK(table.header.find(".staged = &ll_staged_predict") != std::string::npos);
  CHECK(table.header.find(" kLook[]") == std::string::npos);
  CHECK(table.header.find(" kLookEdge[]") == std::string::npos);
  // The functions the table points at are declared there and compiled once,
  // in the parser's own unit.
  const auto staged = std::string_view("::std::uint16_t ll_staged_predict(");
  CHECK(table.header.find(staged) != std::string::npos);
  CHECK(table.header.find("inline ::std::uint16_t ll_staged_predict(") ==
        std::string::npos);
  CHECK(table.header.find("switch (decision)") == std::string::npos);
  CHECK(table.functions.find("switch (decision)") != std::string::npos);
  const auto files =
      cursiv::ll_emit_parser_files(*done, "staged_generation_test", table);
  for (const auto& [name, text] : files)
    CHECK((text.find("switch (decision)") != std::string::npos) ==
          (name == "ParserStates.cpp"));
  auto lowered = cursiv::LlLowering<tllstaged::Base>::lower();
  const auto off = cursiv::ll_build_program(lowered, {.staticPredict = false});
  CHECK(off.nadaptive == off.cfg.ndecisions && off.dfa.empty());
  const auto shallow = cursiv::ll_build_program(lowered, {.depth = 1});
  CHECK(shallow.dfa.empty());
  for (const auto& dec : shallow.cfg.decisions.v)
    if (shallow.cfg.rule_name(dec.rule) == "Unrelated" ||
        shallow.cfg.rule_name(dec.rule) == "AfterSplit")
      CHECK(dec.how == cursiv::LlPredict::adaptive);
}

struct EmbeddedAliasedPrimary : cursiv::GrammarSpec {
  struct Root
      : rule<section<tllindirect::AliasedPrimaryLl>(lit<"[">, lit<"]">) + eof> {
  };
  using entry = Root;
};

void externally_called_expressions_stay_complete() {
  using Aliased = tllindirect::WithAliases<tllindirect::ExposedPrimary>;
  using Reached = cursiv::detail::reached_t<
      cursiv::Normalize<tllindirect::AliasedPrimaryLl>>;
  static_assert(cursiv::detail::tl_has<Aliased::Alias, Reached>::value);
  static_assert(cursiv::detail::tl_has<Aliased::Alias2, Reached>::value);
  static_assert(
      std::is_same_v<
          typename cursiv::detail::home_of<
              cursiv::detail::InfixLevel<tllindirect::Base::Expr, 1>>::type,
          typename cursiv::detail::home_of<tllindirect::Base::Expr>::type>);
  // These entries can need the whole expression inside the wrapper, such
  // as '1+2*'.
  const auto primary = cursiv::ll_build_program(
      cursiv::LlLowering<tllindirect::ExposedPrimary>::lower());
  const auto wrapper = cursiv::ll_build_program(
      cursiv::LlLowering<tllindirect::ExposedWrapper>::lower());
  CHECK(!primary.fault && !primary.overflow);
  CHECK(!wrapper.fault && !wrapper.overflow);
  const auto aliasedPrimary = cursiv::ll_build_program(
      cursiv::LlLowering<tllindirect::AliasedPrimaryLl>::lower());
  const auto aliasedWrapper = cursiv::ll_build_program(
      cursiv::LlLowering<tllindirect::AliasedWrapperLl>::lower());
  CHECK(!aliasedPrimary.fault && !aliasedPrimary.overflow);
  CHECK(!aliasedWrapper.fault && !aliasedWrapper.overflow);
  const auto embeddedPrimary = cursiv::ll_build_program(
      cursiv::LlLowering<EmbeddedAliasedPrimary>::lower());
  CHECK(!embeddedPrimary.fault && !embeddedPrimary.overflow);
}

void recursive_adoption_has_a_finite_set_of_action_chains() {
  const auto built = cursiv::ll_build_program(cursiv::LlLowering<RecursiveAdoption>::lower());
  CHECK(!built.fault && !built.overflow);
  CHECK(built.writes.nchains < 32);
}

void disabling_static_prediction_disables_every_static_automaton() {
  auto low = cursiv::LlLowering<Simple>::lower();
  const auto built = cursiv::ll_build_program(low, {.staticPredict = false});
  CHECK(!built.fault && !built.overflow);
  CHECK(built.cfg.ndecisions != 0);
  CHECK(built.nadaptive == built.cfg.ndecisions);
  CHECK(built.dfa.empty() && built.dfaEdge.empty());
}

void parser_files_have_a_stable_partition_contract() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<Simple>::lower()),
      "generation_test");
  CHECK(done.has_value());
  if (!done) return;
  const auto files = cursiv::ll_emit_parser_files(
      *done, "generation_test",
      cursiv::ll_emit_table(*done, "generation_test"));
  CHECK(files.size() == 10);
  unsigned declarations = 0, entries = 0, units = 0;
  for (const auto& [name, text] : files) {
    // One unit of rules keeps its cold paths: none is shared.
    CHECK(text.find("COLD_PATHS") == std::string::npos);
    if (name == "ParserRules.hpp") {
      ++declarations;
      // The one rule calls none: a plain function, with no frame.
      CHECK(text.find("void s1(::cursiv::LlContext&);") != std::string::npos);
      CHECK(text.find("struct F") == std::string::npos);
    } else if (name == "ParserStates.cpp") {
      ++entries;
      CHECK(text.find("parse_entry") != std::string::npos);
      CHECK(text.find("LlMachine") != std::string::npos);
      // The prediction runtime only where the unit defines the table's
      // staged predictor.
      CHECK((text.find("ll_predictor.hpp") != std::string::npos) ==
            (text.find("ll_staged_predict(") != std::string::npos));
    } else if (name.starts_with("ParserRules") && name.ends_with(".cpp")) {
      ++units;
      CHECK(text.find("void r") == std::string::npos);
    }
  }
  CHECK(declarations == 1 && entries == 1 && units == 8);
}

// Choices whose first arm opens on a keyword and holds the next choice: each
// arm is spelt once, so the source grows with the nesting and not with its
// power.
template <unsigned N>
constexpr auto keyword_choice() {
  if constexpr (N == 0)
    return cursiv::kw<"z">;
  else
    return (cursiv::kw<"p"> + keyword_choice<N - 1>()) | cursiv::kw<"b">;
}
template <unsigned N>
struct KeywordChoices : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Node>(many(keyword_choice<N>()))> {};
  using entry = Root;
  using ignored = WS;
};

// The parser files of a grammar, by name.
template <class G>
std::vector<std::pair<std::string, std::string>> emitted_files() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<G>::lower()), "emit_test");
  CHECK(done.has_value());
  if (!done) return {};
  return cursiv::ll_emit_parser_files(
      *done, "emit_test", cursiv::ll_emit_table(*done, "emit_test"));
}

// The bytes of a grammar's parser files, or of its rule units alone.
template <class G>
std::size_t emitted_size(bool rulesOnly = false) {
  std::size_t size = 0;
  for (const auto& [name, text] : emitted_files<G>())
    if (!rulesOnly ||
        (name.starts_with("ParserRules") && name.ends_with(".cpp")))
      size += text.size();
  return size;
}

void nested_choices_spell_each_arm_once() {
  const std::size_t four = emitted_size<KeywordChoices<4>>(),
                    twelve = emitted_size<KeywordChoices<12>>();
  CHECK(four != 0 && twelve < 4 * four);
}

// A choice of keywords alone is a table: a keyword more costs the source a
// table entry and a case, not a reading of its own.
constexpr auto kFourWords =
    cursiv::kw<"a"> | cursiv::kw<"b"> | cursiv::kw<"c"> | cursiv::kw<"d">;
constexpr auto kSixteenWords =
    kFourWords | cursiv::kw<"e"> | cursiv::kw<"f"> | cursiv::kw<"g"> |
    cursiv::kw<"h"> | cursiv::kw<"i"> | cursiv::kw<"j"> | cursiv::kw<"k"> |
    cursiv::kw<"l"> | cursiv::kw<"m"> | cursiv::kw<"n"> | cursiv::kw<"o"> |
    cursiv::kw<"p">;
template <bool Wide>
struct WordChoices : cursiv::GrammarSpec {
  static constexpr auto Words = [] {
    if constexpr (Wide)
      return kSixteenWords;
    else
      return kFourWords;
  }();
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Node>(many(lit<"("> + Words + lit<")">))> {};
  using entry = Root;
  using ignored = WS;
};

void keyword_choices_are_tables() {
  const std::size_t four = emitted_size<WordChoices<false>>(true),
                    sixteen = emitted_size<WordChoices<true>>(true);
  CHECK(four != 0 && sixteen < four + 12 * 128);
}

// A choice whose arms each spell a long run of choices: each arm is a
// function of its own.
template <unsigned N>
constexpr auto word_run() {
  if constexpr (N == 1)
    return kSixteenWords;
  else
    return kSixteenWords + word_run<N - 1>();
}
struct LongArms : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Node>(many((lit<"x"> + word_run<12>()) |
                                       (lit<"y"> + word_run<12>())))> {};
  using entry = Root;
  using ignored = WS;
};

void long_arms_are_functions() {
  unsigned arms = 0;
  for (const auto& [name, text] : emitted_files<LongArms>())
    if (name == "ParserRules.hpp")
      for (const char* fn : {"void s1_0(::cursiv::LlContext&);",
                             "void s1_1(::cursiv::LlContext&);"})
        arms += text.find(fn) != std::string::npos;
  CHECK(arms == 2);
}

// ...and where the arms call a rule that calls itself, each arm is a step of
// its own, with its frame, which the machine runs as it runs a rule.
struct LongSuspendingArms : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Nest : rule<lit<"("> + option(ref<Nest>) + lit<")">> {};
  struct Root
      : rule<create<Node>(many((lit<"x"> + word_run<12>() + ref<Nest>) |
                               (lit<"y"> + word_run<12>() + ref<Nest>)))> {};
  using entry = Root;
  using ignored = WS;
};

void long_suspending_arms_are_steps() {
  std::string declarations, states;
  for (const auto& [name, text] : emitted_files<LongSuspendingArms>())
    if (name == "ParserRules.hpp")
      declarations = text;
    else if (name == "ParserStates.cpp")
      states = text;
  // The rules span several units, which share their cold paths: declared for
  // each, compiled in the parser's own unit.
  CHECK(
      declarations.find("CURSIV_LL_COLD_PATHS(extern, ::emit_test::kTable);") !=
      std::string::npos);
  CHECK(states.find("CURSIV_LL_COLD_PATHS(, ::emit_test::kTable);") !=
        std::string::npos);
  for (const char* tag : {"1_0", "1_1"}) {
    CHECK(declarations.find(std::format("struct F{} {{", tag)) !=
          std::string::npos);
    CHECK(declarations.find(std::format(
              "::std::uint16_t s{}(::cursiv::LlContext&, void*);", tag)) !=
          std::string::npos);
    CHECK(states.find(std::format("return s{}(c, f);", tag)) !=
          std::string::npos);
    CHECK(states.find(std::format("sizeof(F{})", tag)) != std::string::npos);
  }
}

// A chain of rules, each calling the next before a token, as long as node rows
// allow: only its deepest links run as plain functions, as many as the limit
// allows. A rule that calls itself has a frame, and so does its caller.
void plain_calls_stack_no_deeper_than_the_limit() {
  constexpr unsigned links = 20000;
  cursiv::LlLowCfg cfg;
  const auto add = [&](cursiv::LlNode n) {
    cfg.nodes[cfg.nnodes] = n;
    return static_cast<std::uint16_t>(cfg.nnodes++);
  };
  const auto call_then_token = [&](unsigned callee) {
    cfg.kids[cfg.nkids++] = add({.kind = cursiv::LlShape::call,
                                 .sym = static_cast<std::uint16_t>(callee)});
    cfg.kids[cfg.nkids++] = add({.kind = cursiv::LlShape::term, .sym = 1});
    return add({.kind = cursiv::LlShape::seq,
                .sym = static_cast<std::uint16_t>(cfg.nkids - 2),
                .count = 2});
  };
  cfg.nrules = links + 2;
  for (unsigned r = 0; r + 1 < links; ++r)
    cfg.rules[r].body = call_then_token(r + 1);
  cfg.rules[links - 1].body = add({.kind = cursiv::LlShape::term, .sym = 1});
  cfg.rules[links].body = call_then_token(links);
  cfg.rules[links + 1].body = call_then_token(links);
  const auto depth = cursiv::ll_plain_depths(cfg);
  CHECK(depth[links - 1] == 1);
  CHECK(depth[links - cursiv::kLlPlainDepth] == cursiv::kLlPlainDepth);
  CHECK(std::ranges::count(depth, 0) + cursiv::kLlPlainDepth == cfg.nrules);
}

template <class G>
void check_return_nodes() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<G>::lower()),
      "return_nodes_test");
  CHECK(done.has_value());
  if (!done) return;
  const auto& built = done->program();
  CHECK(built.returnNode.size() == built.nstates);
  std::vector<std::uint16_t> expected(built.nstates, cursiv::kNoRow);
  std::vector<bool> used(built.nstates);
  for (std::uint32_t state = 0; state < built.nstates; ++state) {
    const auto& q = built.states[state];
    for (std::uint32_t i = 0; i < q.nedges; ++i) {
      const auto& edge = built.edges[q.edge + i];
      if (edge.kind != cursiv::LlEdgeKind::rule) continue;
      CHECK(edge.follow < built.nstates && !used[edge.follow]);
      used[edge.follow] = true;
      expected[edge.follow] = built.edgeNode[q.edge + i];
    }
  }
  CHECK(built.returnNode == expected);
  const auto table = cursiv::ll_emit_table(*done, "return_nodes_test").header;
  CHECK(table.find(" kReturnNode[]") != std::string::npos);
  CHECK(table.find(".returnNode = kReturnNode") != std::string::npos);
}

void return_states_preserve_named_and_synthetic_calls() {
  check_return_nodes<tllcaretagree::Ll>();
  check_return_nodes<tllindirect::Ll>();
  check_return_nodes<LocalGuardChoices>();
}

void a_table_holds_only_the_data_its_readers_ask_for() {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<tllstaged::Base>::lower()),
      "table_data_test");
  CHECK(done.has_value());
  if (!done) return;
  CHECK(done->program().cfg.ngapConds != 0);
  const auto source = cursiv::ll_emit_table(*done, "table_data_test");
  const std::string& table = source.header;
  // The names are the terminals', not the rules'.
  CHECK(table.find("WS") != std::string::npos);
  CHECK(table.find("AfterSplit") == std::string::npos);
  // No word class or scalar range to decode, no keyword to reserve, and gaps
  // that read the sets alone.
  CHECK(table.find("kSetDecode") == std::string::npos);
  CHECK(table.find("kLexRes") == std::string::npos);
  CHECK(table.find(".reserves") == std::string::npos);
  CHECK(table.find("kGapBoundary") == std::string::npos);
  CHECK(table.find(" kLexAcc[]") != std::string::npos);
  // The condition is declared beside the data and defined in the parser's own
  // unit.
  CHECK(table.find("\nbool condition(") != std::string::npos);
  CHECK(table.find("switch (k)") == std::string::npos);
  CHECK(source.functions.find("bool condition(") != std::string::npos);
  CHECK(source.functions.find("inline bool condition(") == std::string::npos);
}

// The functions of the byte recognizers a table's unit defines, each by its
// body with its own name taken out.
template <class G>
std::vector<std::string> lexical_bodies(const char* ns) {
  const auto done = cursiv::LlFinalProgram::make(
      cursiv::ll_build_program(cursiv::LlLowering<G>::lower()), ns);
  CHECK(done.has_value());
  if (!done) return {};
  const std::string text = cursiv::ll_emit_table(*done, ns).functions;
  std::vector<std::string> bodies;
  for (std::size_t at = text.find("inline "); at != std::string::npos;
       at = text.find("inline ", at + 1)) {
    const std::size_t name = text.find("lexical_", at);
    const std::size_t open = text.find('(', at);
    if (name == std::string::npos || open < name ||
        (text.compare(name, 13, "lexical_fixed") != 0 &&
         text.compare(name, 12, "lexical_node") != 0))
      continue;
    const std::size_t end = text.find("\n}\n", open);
    bodies.push_back(text.substr(at, name - at) +
                     text.substr(open, end - open));
  }
  return bodies;
}

// A recognizer is spelled once per shape, however often the grammar repeats
// it, and a choice of more words than a mask holds, as a class of characters
// over several bytes lowers to, is read as data.
void byte_recognizers_spell_each_shape_once() {
  auto bodies = lexical_bodies<tlexicalpredicates::Ll>("shape_test");
  CHECK(!bodies.empty());
  std::ranges::sort(bodies);
  CHECK(std::ranges::adjacent_find(bodies) == bodies.end());
  const auto words = lexical_bodies<tguardedclimb::Ll>("words_test");
  CHECK(!words.empty() && words.size() < 32);
  CHECK(std::ranges::any_of(words, [](const std::string& body) {
    return body.find("::cursiv::lexical_words<") != std::string::npos;
  }));
}

}  // namespace

int main() {
  record_sources_keep_nullable_suffixes_and_only_required_children();
  record_aliases_propagate_changes_without_rejoining_every_rule();
  record_rows_keep_the_order_of_first_arrival_through_aliases();
  unreadable_gap_operands_are_refused_during_lowering();
  only_members_of_a_recursive_component_are_named();
  exploration_reports_a_limit_instead_of_an_incomplete_answer();
  externally_called_expressions_stay_complete();
  disabling_static_prediction_disables_every_static_automaton();
  recursive_adoption_has_a_finite_set_of_action_chains();
  a_guard_only_disables_the_static_decisions_that_can_reach_it();
  classification_stops_before_a_guard_can_choose_an_arm();
  parser_files_have_a_stable_partition_contract();
  nested_choices_spell_each_arm_once();
  keyword_choices_are_tables();
  long_arms_are_functions();
  long_suspending_arms_are_steps();
  plain_calls_stack_no_deeper_than_the_limit();
  return_states_preserve_named_and_synthetic_calls();
  a_table_holds_only_the_data_its_readers_ask_for();
  byte_recognizers_spell_each_shape_once();
  return cursiv::test::report("LlGeneration_test");
}
