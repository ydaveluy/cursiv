#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/gen/ll_emit_parser.hpp"
#include "cursiv/engines/ll/gen/ll_emit_table.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/InfixLevel.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/CaretAgreementGrammar.hpp"
#include "engines/ll/IndirectFoldGrammar.hpp"
#include "engines/ll/StagedGuardGrammar.hpp"

namespace {

void only_members_of_a_recursive_component_are_marked() {
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
  CHECK(!built.cfg.ruleBuild[1].leftCyclic);
  CHECK(built.cfg.ruleBuild[2].leftCyclic && built.cfg.ruleBuild[3].leftCyclic);
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
  const auto built = cursiv::ll_build_program(
      cursiv::LlLowering<LocalGuardChoices>::lower());
  CHECK(!built.fault && !built.overflow && built.cfg.ngapConds != 0);
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
  const cursiv::LlStampPlan plan(built);
  const auto files =
      cursiv::ll_emit_parser_files(built, plan, "local_guard_test");
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
  const auto built = cursiv::ll_build_program(cursiv::LlLowering<tllstaged::Base>::lower());
  CHECK(!built.fault && !built.overflow && built.cfg.ngapConds != 0);
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
  const cursiv::LlStampPlan plan(built);
  const auto table =
      cursiv::ll_emit_table(built, plan, "staged_generation_test");
  CHECK(table.find(".staged = &ll_staged_predict") != std::string::npos);
  CHECK(table.find(" kLook[]") == std::string::npos);
  CHECK(table.find(" kLookEdge[]") == std::string::npos);
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
  const auto built = cursiv::ll_build_program(cursiv::LlLowering<Simple>::lower());
  const cursiv::LlStampPlan plan(built);
  const auto files =
      cursiv::ll_emit_parser_files(built, plan, "generation_test");
  CHECK(files.size() == 10);
  unsigned declarations = 0, entries = 0, units = 0;
  for (const auto& [name, text] : files) {
    if (name == "ParserRules.hpp") {
      ++declarations;
      CHECK(text.find("::std::uint16_t s") != std::string::npos);
      CHECK(text.find("struct F") != std::string::npos);
    } else if (name == "ParserStates.cpp") {
      ++entries;
      CHECK(text.find("parse_entry") != std::string::npos);
      CHECK(text.find("LlMachine") != std::string::npos);
      CHECK(text.find("ll_predictor.hpp") == std::string::npos);
    } else if (name.starts_with("ParserRules") && name.ends_with(".cpp")) {
      ++units;
      CHECK(text.find("void r") == std::string::npos);
    }
  }
  CHECK(declarations == 1 && entries == 1 && units == 8);
}

template <class G>
void check_return_nodes() {
  const auto built = cursiv::ll_build_program(cursiv::LlLowering<G>::lower());
  CHECK(!built.fault && !built.overflow);
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
  const cursiv::LlStampPlan plan(built);
  const auto table = cursiv::ll_emit_table(built, plan, "return_nodes_test");
  CHECK(table.find(" kReturnNode[]") != std::string::npos);
  CHECK(table.find(".returnNode = kReturnNode") != std::string::npos);
}

void return_states_preserve_named_and_synthetic_calls() {
  check_return_nodes<tllcaretagree::Ll>();
  check_return_nodes<tllindirect::Ll>();
  check_return_nodes<LocalGuardChoices>();
}

}  // namespace

int main() {
  unreadable_gap_operands_are_refused_during_lowering();
  only_members_of_a_recursive_component_are_marked();
  exploration_reports_a_limit_instead_of_an_incomplete_answer();
  externally_called_expressions_stay_complete();
  disabling_static_prediction_disables_every_static_automaton();
  recursive_adoption_has_a_finite_set_of_action_chains();
  a_guard_only_disables_the_static_decisions_that_can_reach_it();
  classification_stops_before_a_guard_can_choose_an_arm();
  parser_files_have_a_stable_partition_contract();
  return_states_preserve_named_and_synthetic_calls();
  return cursiv::test::report("LlGeneration_test");
}
