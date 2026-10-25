#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "compoundll/generated/Ll/Module.hpp"
#include "compoundll/generated/Ll/Table.hpp"
#include "compoundlr/generated/Lr/Module.hpp"
#include "compoundplainll/generated/PlainLl/Module.hpp"
#include "compoundplainlr/generated/PlainLr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/engines/ll/runtime/ll_search.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/CompoundOpGrammar.hpp"

// A deletion leaves the positions unchanged but asks their guards at another
// token. The discarded child's node slot may also be reused by that deletion.
void guarded_offers_follow_the_token() {
  using namespace cursiv;
  constexpr const auto& table = tllcompound::generated::Ll::kTable;
  std::uint32_t guard = kLlNoNode;
  for (std::uint32_t q = 0; q < table.atn.nstates && guard == kLlNoNode; ++q) {
    const auto& state = table.atn.state(q);
    for (std::uint32_t e = 0; e < state.nedges; ++e)
      if (table.atn.edge(state.edge + e).kind == LlEdgeKind::condition) {
        guard = q;
        break;
      }
  }
  CHECK(guard != kLlNoNode);
  if (guard == kLlNoNode) return;
  for (const bool reuse : {false, true}) {
    Cst tree("1 in \n in 2");
    CstBuilder builder(tree);
    LlContext c(tree.text(), builder);
    LlSearch search(table.atn, c.len);
    search.limit = 10000;
    search.sim.resume();
    search.sim.seed(guard, kLlCtxNone);
    LlReading source;
    source.ahead.p = source.gapFrom = 1;
    source.tokens = 1;
    ll_search_keep<table>(search, source);
    search.nodes.push_back(source);
    const LlSearchLaid before(search);
    search.nodes.push_back(source);
    const auto& sameLine = ll_search_facts<table>(c, search, 1);
    CHECK(!sameLine.viable.empty() && !sameLine.due.empty());
    if (reuse) ll_search_trim(search, before);
    source.ahead.p = source.gapFrom = 4;
    search.nodes.push_back(source);
    const auto next = static_cast<std::uint32_t>(search.nodes.size() - 1);
    const auto& nextLine = ll_search_facts<table>(c, search, next);
    CHECK(nextLine.viable.empty() && nextLine.due.empty());
  }
}

int main() {
  guarded_offers_follow_the_token();
  const auto ll = tllcompound::generated::Ll::create_parser();
  const auto lr = tllcompound::generated::Lr::create_parser();
  const auto reflection = tllcompound::generated::Ll::create_ast_reflection();
  for (const std::string_view text :
       {"1", "1+2+3", "1 not in 2", "1 not like 2", "1->2", "1-2->3", "1->2-3",
        "1->2->3", "1 - > 2", "1,2", "{a:1}", "{a:1,b:2}", "{a:(1,2),b:3}",
        "{a:{b:1,c:2},d:3}", "1 not/* comment */ in 2+3",
        "1+2 not like 3 not in 4", "1\nnot in 2", "1 not", "1 not other 2"}) {
    const auto a = ll->parse(text), b = lr->parse(text);
    const bool cleanA = a.accepted && a.diagnostics.empty();
    const bool cleanB = b.accepted && b.diagnostics.empty();
    if (cleanA != cleanB)
      std::printf("compound parity LL=%d LR=%d: %.*s\n", cleanA, cleanB,
                  static_cast<int>(text.size()), text.data());
    CHECK(cleanA == cleanB);
    if (cleanA && cleanB) {
      CHECK(a.value != nullptr && b.value != nullptr);
      std::string x, y;
      cursiv::test::ast_of(*reflection, *a.value, x);
      cursiv::test::ast_of(*reflection, *b.value, y);
      CHECK(x == y);
    }
  }
  // A missing caller's closer can leave an adaptive operator choice with
  // no complete arm at EOF. It must stop the infix, preserving the operand
  // for the caller's local repair rather than consuming EOF as an operator.
  for (const std::string_view text : {"{a:1", "{a:(1", "(1", "{a:1 not"}) {
    const auto parsed = ll->parse(text);
    CHECK(!parsed.diagnostics.empty());
    CHECK(parsed.cst().size() != 0 && parsed.cst().size() < 256);
    for (cursiv::NodeId i = 0; i < parsed.cst().size(); ++i) {
      const auto& node = parsed.cst()[i];
      CHECK(node.begin <= node.end && node.end <= text.size());
      CHECK(node.subtreeSize != 0 && node.subtreeSize <= i + 1u);
    }
  }
  for (const auto& parser : {ll.get(), lr.get()}) {
    const auto result = parser->parse("1 not in 2+3");
    const auto* doc = cursiv::ast_cast<tllcompound::Document>(result.value);
    CHECK(doc != nullptr);
    if (doc == nullptr) continue;
    const auto* outer = cursiv::ast_cast<tllcompound::Binary>(doc->value);
    CHECK(outer != nullptr && outer->op == tllcompound::Op::not_in);
    if (outer == nullptr) continue;
    const auto* inner = cursiv::ast_cast<tllcompound::Binary>(outer->right);
    CHECK(inner != nullptr && inner->op == tllcompound::Op::add);
    const auto arrow = parser->parse("1-2->3");
    const auto* arrowDoc = cursiv::ast_cast<tllcompound::Document>(arrow.value);
    CHECK(arrow.accepted && arrow.diagnostics.empty() && arrowDoc != nullptr);
    if (arrowDoc == nullptr) continue;
    const auto* implication =
        cursiv::ast_cast<tllcompound::Binary>(arrowDoc->value);
    CHECK(implication != nullptr &&
          implication->op == tllcompound::Op::implies);
    if (implication == nullptr) continue;
    const auto* subtraction =
        cursiv::ast_cast<tllcompound::Binary>(implication->left);
    CHECK(subtraction != nullptr && subtraction->op == tllcompound::Op::sub);
    const auto object = parser->parse("{a:(1,2),b:3}");
    const auto* objectDoc =
        cursiv::ast_cast<tllcompound::Document>(object.value);
    CHECK(object.accepted && object.diagnostics.empty() &&
          objectDoc != nullptr);
    if (objectDoc == nullptr) continue;
    const auto* fields =
        cursiv::ast_cast<tllcompound::Object>(objectDoc->value);
    CHECK(fields != nullptr && fields->pairs.size() == 2);
    if (fields == nullptr || fields->pairs.size() != 2) continue;
    CHECK(fields->pairs[0]->key == "a" && fields->pairs[1]->key == "b");
    const auto* comma =
        cursiv::ast_cast<tllcompound::Binary>(fields->pairs[0]->value);
    CHECK(comma != nullptr && comma->op == tllcompound::Op::comma);
  }
  // A repair the two engines can both make is the same repair: one note per
  // fault, of one kind over one span, and one tree.
  const auto same = [](const cursiv::Parser& ll, const cursiv::Parser& lr,
                       const cursiv::AstReflection& reflection,
                       std::string_view text) {
    const auto a = ll.parse(text), b = lr.parse(text);
    bool alike = a.diagnostics.size() == b.diagnostics.size();
    for (std::size_t i = 0; alike && i < a.diagnostics.size(); ++i)
      alike = a.diagnostics[i].kind == b.diagnostics[i].kind &&
              a.diagnostics[i].begin == b.diagnostics[i].begin &&
              a.diagnostics[i].end == b.diagnostics[i].end;
    std::string x, y;
    if (a.value != nullptr) cursiv::test::ast_of(reflection, *a.value, x);
    if (b.value != nullptr) cursiv::test::ast_of(reflection, *b.value, y);
    if (!alike || x != y)
      std::printf("repair parity LL=%zu LR=%zu: %.*s\n", a.diagnostics.size(),
                  b.diagnostics.size(), static_cast<int>(text.size()),
                  text.data());
    CHECK(alike && x == y);
  };
  const auto plainLl = tllcompound::generated::PlainLl::create_parser();
  const auto plainLr = tllcompound::generated::PlainLr::create_parser();
  const auto plainReflection =
      tllcompound::generated::PlainLl::create_ast_reflection();
  for (const std::string_view text :
       {"1 not in 2", "1+2 not in 3+4", "1 not in 2 not in 3", "1 not 2",
        "1 not in", "1 in 2", "1 2", "1 2 3", "1 in 2 3", "1 in", "1 in in"})
    same(*plainLl, *plainLr, *plainReflection, text);
  for (const std::string_view text :
       {"1 in 2", "1 2", "1 2 3", "1+2 3", "(1 2)", "1 in", "1 in in", "1 )",
        "1 ) )", "{a:1 b:2}"})
    same(*ll, *lr, *reflection, text);
  const auto plain = plainLl->parse("1+2 not in 3");
  CHECK(plain.diagnostics.empty());
  const auto* plainDoc = cursiv::ast_cast<tllcompound::Document>(plain.value);
  CHECK(plainDoc != nullptr);
  if (plainDoc != nullptr) {
    const auto* outer = cursiv::ast_cast<tllcompound::Binary>(plainDoc->value);
    CHECK(outer != nullptr && outer->op == tllcompound::Op::not_in);
  }
  return cursiv::test::report("LlCompoundOp_test");
}
