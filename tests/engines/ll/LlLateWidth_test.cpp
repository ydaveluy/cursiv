#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/ll_indent.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionSession.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/LateWidthAst.hpp"
#include "latewidthll/generated/Ll/Module.hpp"
#include "latewidthll/generated/Ll/Table.hpp"

namespace {
using namespace tlllatewidth;

void expect_value(const cursiv::ParseResult& parsed, unsigned count,
                  bool dimension) {
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<Document>();
  CHECK(doc && doc->value);
  if (!doc || !doc->value) return;
  const auto* value = doc->value;
  CHECK(doc->parent == nullptr && value->parent == doc);
  CHECK(value->prefix.size() == count);
  const auto span = [&](const cursiv::AstNode* node) {
    CHECK(node->cstNodeId < parsed.cst().size());
    if (node->cstNodeId < parsed.cst().size()) {
      const auto& cst = parsed.cst()[node->cstNodeId];
      CHECK(cst.begin <= cst.end && cst.end <= (count * 5 + (dimension ? 4 : 2)));
    }
  };
  span(doc);
  span(value);
  for (const auto* word : value->prefix) {
    CHECK(word && word->name == "word" && word->parent == value);
    if (word) span(word);
  }
  if (dimension) {
    const auto* d = cursiv::ast_cast<DimensionValue>(value);
    CHECK(d && d->dimension == "5px");
  } else {
    const auto* n = cursiv::ast_cast<NumberValue>(value);
    CHECK(n && n->number == "5");
  }
}

void a_long_shared_prefix_is_read_whole() {
  constexpr const auto& table = tlllatewidth::generated::Ll::kTable;
  const auto entry = table.atn.outgoing(table.atnStart).front().target;
  const auto decision = table.atn.state(entry).decision;
  CHECK(decision < table.ndecisions);
  if (decision >= table.ndecisions) return;
  CHECK(table.decisions[decision].state == entry &&
        table.decisions[decision].how == cursiv::LlPredict::adaptive);
  std::string text;
  for (unsigned i = 0; i < 128; ++i) text += "word;";
  text += "5px!";
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  cursiv::LlContext context(text, builder);
  cursiv::ll_peek<table>(context, table.decisions[decision].mode);
  cursiv::LlPredictionSession<table> session;
  const auto selected = cursiv::ll_episode<table>(context, decision);
  CHECK(selected != cursiv::kNoRow);
  CHECK(session.work().tokens >= 128);
}
}  // namespace
int main() {
  constexpr const auto& table = tlllatewidth::generated::Ll::kTable;
  static_assert(table.ngapConds == 0 && !cursiv::kIndented<table>);
  const auto ll = tlllatewidth::generated::Ll::create_parser();
  for (unsigned n : {1u, 2u, 32u, 128u, 512u, 768u}) {
    std::string prefix;
    for (unsigned i = 0; i < n; ++i) prefix += "word;";
    // Both segmentations are syntactically viable. Keep the longest-token
    // preference and the Dimension creator after the long shared prefix.
    expect_value(ll->parse(prefix + "5px!"), n, true);
    // Only NUMBER matches here.
    expect_value(ll->parse(prefix + "5?"), n, false);
  }
  a_long_shared_prefix_is_read_whole();
  return cursiv::test::report("LlLateWidth_test");
}
