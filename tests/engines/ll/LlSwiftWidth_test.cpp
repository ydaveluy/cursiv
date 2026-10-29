#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionSession.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/LlTerms.hpp"
#include "engines/ll/SwiftWidthAst.hpp"
#include "swiftwidthll/generated/Ll/Module.hpp"
#include "swiftwidthll/generated/Ll/Table.hpp"
namespace {
using namespace tllswiftwidth;
void a_completed_entry_survives_an_unfinished_shorter_reading() {
  const auto ll = generated::Ll::create_parser();
  for (const std::string_view text :
       {"#\"\"\"\nx\n\"\"\"#\n", "#\"\"\"\n\"\"\"#\n",
        "#\"\"\"a\"b\"\"c\"\"\"#\n", "g#\"\"\"x\"\"\"#\n",
        "g#\"\"\"\n\"\"\"#\n", "##\"\"\"x\"\"\"##\n", "#\"\"\"x\"\"\"#\n",
        "#\"\"\"a\"\"\"#\n#\"\"\"b\"\"\"#\n"}) {
    const auto parsed = ll->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    const auto* doc = parsed.value_as<Document>();
    CHECK(doc && doc->parent == nullptr);
    if (!doc) continue;
    CHECK(doc->values.size() == (text == "#\"\"\"a\"\"\"#\n#\"\"\"b\"\"\"#\n" ? 2u : 1u));
    for (const auto* value : doc->values) {
      CHECK(value && value->parent == doc);
      if (!value) continue;
      CHECK(value->opening == (text.starts_with("##") ? "##\"\"\"" : "#\"\"\""));
      CHECK(value->cstNodeId < parsed.cst().size());
      if (value->cstNodeId < parsed.cst().size()) {
        const auto& span = parsed.cst()[value->cstNodeId];
        CHECK(span.begin < span.end && span.end <= text.size());
      }
    }
  }

  for (const std::string_view text :
       {"#\"\"\"x\"\"#", "#\"\"\"x\"\"\"", "#\"\"\"x\"\"\"?"}) {
    CHECK(!ll->parse(text).diagnostics.empty());
  }
  constexpr const auto& table = generated::Ll::kTable;
  CHECK(table.ngapConds != 0);
}
void a_closing_walk_reads_the_closer() {
  constexpr const auto& table = generated::Ll::kTable;
  std::uint16_t decision = cursiv::kNoRow;
  const std::uint16_t close = ll_terms::named<table>("Close");
  // Value's loop leaves directly for Close; GuardedValue has a condition there.
  for (std::uint16_t i = 0; i < table.ndecisions; ++i)
    for (const auto& exit : table.atn.outgoing(table.decisions[i].state))
      if (exit.kind == cursiv::LlEdgeKind::epsilon)
        for (const auto& edge : table.atn.outgoing(exit.target))
          if (edge.kind == cursiv::LlEdgeKind::atom && edge.label == close) {
            CHECK(decision == cursiv::kNoRow);
            decision = i;
          }
  CHECK(decision != cursiv::kNoRow && close != cursiv::kInvalidSymbol);
  if (decision == cursiv::kNoRow || close == cursiv::kInvalidSymbol) return;
  const auto& dec = table.decisions[decision];
  const std::string_view text = "#\"\"\"\nx\n\"\"\"#\n";
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  cursiv::LlContext context(text, builder);
  context.at = 7;
  context.la = {7, 11, close};
  context.laValid = true;
  context.laMode = dec.mode;
  context.document = true;
  context.stack.push_back(
      table.atn.callerAt[table.atn.callerBase[table.atn.state(dec.state).rule]]);
  {
    cursiv::LlPredictionSession<table> session;
    CHECK(cursiv::ll_episode<table>(context, decision) == 1);
  }
  {
    cursiv::LlPredictionSession<table> session;
    CHECK(cursiv::ll_predict<table>(context, decision) == 1);
  }
}
}  // namespace
int main() {
  a_completed_entry_survives_an_unfinished_shorter_reading();
  a_closing_walk_reads_the_closer();
  return cursiv::test::report("LlSwiftWidth_test");
}
