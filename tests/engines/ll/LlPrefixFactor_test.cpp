#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/PrefixFactorGrammar.hpp"
#include "prefixfactorll/generated/Ll/Module.hpp"
#include "prefixfactorlr/generated/Lr/Module.hpp"

namespace {

using namespace tllprefix;

void same_node(const Expression* a, const Expression* b,
               const cursiv::AstNode* parentA, const cursiv::AstNode* parentB,
               const cursiv::Cst& treeA, const cursiv::Cst& treeB) {
  CHECK(a != nullptr && b != nullptr);
  if (a == nullptr || b == nullptr) return;
  CHECK(a->parent == parentA && b->parent == parentB);
  CHECK(a->cstNodeId < treeA.size() && b->cstNodeId < treeB.size());
  if (a->cstNodeId < treeA.size() && b->cstNodeId < treeB.size()) {
    const auto& spanA = treeA[a->cstNodeId];
    const auto& spanB = treeB[b->cstNodeId];
    CHECK(spanA.begin == spanB.begin && spanA.end == spanB.end);
  }
  CHECK(a->type == b->type);
  if (a->type != b->type) return;
  if (const auto* x = cursiv::ast_cast<Name>(a)) {
    CHECK(x->text == cursiv::ast_cast<Name>(b)->text);
  } else if (const auto* x = cursiv::ast_cast<Unary>(a)) {
    const auto* y = cursiv::ast_cast<Unary>(b);
    CHECK(x->enabled == y->enabled);
    same_node(x->value, y->value, x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<Wrapped>(a)) {
    const auto* y = cursiv::ast_cast<Wrapped>(b);
    same_node(x->value, y->value, x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<Bag>(a)) {
    const auto* y = cursiv::ast_cast<Bag>(b);
    CHECK(x->values.size() == 1 && y->values.size() == 1);
    if (x->values.size() == 1 && y->values.size() == 1)
      same_node(x->values[0], y->values[0], x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<LeftTag>(a)) {
    const auto* y = cursiv::ast_cast<LeftTag>(b);
    CHECK(x->enabled && y->enabled);
    same_node(x->operand, y->operand, x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<RightTag>(a)) {
    const auto* y = cursiv::ast_cast<RightTag>(b);
    CHECK(x->enabled && y->enabled);
    CHECK(x->parts.size() == 1 && y->parts.size() == 1);
    if (x->parts.size() == 1 && y->parts.size() == 1)
      same_node(x->parts[0], y->parts[0], x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<Let>(a)) {
    const auto* y = cursiv::ast_cast<Let>(b);
    same_node(x->value, y->value, x, y, treeA, treeB);
  } else if (const auto* x = cursiv::ast_cast<Maybe>(a)) {
    const auto* y = cursiv::ast_cast<Maybe>(b);
    CHECK(x->enabled == y->enabled);
    same_node(x->value, y->value, x, y, treeA, treeB);
  }
}

void outer_choice_keeps_its_priority(const cursiv::Parser& parser) {
  for (const std::string_view text : {"x", "x y", "x y y", "rc x y"}) {
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* document = result.value_as<Document>();
    CHECK(document != nullptr && document->values.size() == 1);
    if (document == nullptr || document->values.size() != 1) continue;
    const auto* wrapped = cursiv::ast_cast<NS>(document->values[0]);
    const bool expectsWrapper = text == "x y" || text == "x y y";
    CHECK((wrapped != nullptr) == expectsWrapper);
    const auto* child = wrapped != nullptr
                            ? wrapped->value
                            : cursiv::ast_cast<RC>(document->values[0]);
    CHECK(child != nullptr);
    if (child == nullptr) continue;
    CHECK(child->y == (text == "x y y" || text == "rc x y"));
    CHECK(document->parent == nullptr);
    CHECK(child->parent == (wrapped != nullptr
                                ? static_cast<const cursiv::AstNode*>(wrapped)
                                : document));
    CHECK(child->cstNodeId < result.cst().size());
    if (child->cstNodeId < result.cst().size()) {
      const auto& span = result.cst()[child->cstNodeId];
      // DirectRc adopts RC, so its record includes the leading `rc`.
      CHECK(span.begin == 0);
      CHECK(span.end == (text == "x y y" ? 3u : text == "rc x y" ? 6u : 1u));
    }
    if (wrapped != nullptr) {
      CHECK(wrapped->y && wrapped->parent == document);
      CHECK(wrapped->cstNodeId < result.cst().size());
      if (wrapped->cstNodeId < result.cst().size()) {
        const auto& outer = result.cst()[wrapped->cstNodeId];
        CHECK(outer.begin == 0 && outer.end == text.size());
      }
    }
  }
  // Appending the shared child preserves the same outer priority.
  for (const std::string_view text : {"append x y", "append x y y"}) {
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* document = result.value_as<Document>();
    CHECK(document != nullptr && document->values.size() == 1);
    if (document == nullptr || document->values.size() != 1) continue;
    const auto* wrapped = cursiv::ast_cast<NSAppend>(document->values[0]);
    CHECK(wrapped != nullptr && wrapped->values.size() == 1);
    if (wrapped == nullptr || wrapped->values.size() != 1) continue;
    const auto* child = wrapped->values[0];
    CHECK(child != nullptr);
    CHECK(wrapped->y && wrapped->parent == document);
    if (child != nullptr) {
      CHECK(child->y == (text == "append x y y"));
      CHECK(child->parent == wrapped);
    }
  }
  // Reversing RS prefers the bare RC on x y; a wrapper is still required
  // on x y y to reach the explicit end of this construct.
  for (const std::string_view text :
       {"reverse x;", "reverse x y;", "reverse x y y;"}) {
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* document = result.value_as<Document>();
    CHECK(document != nullptr && document->values.size() == 1);
    if (document == nullptr || document->values.size() != 1) continue;
    const auto* wrapped = cursiv::ast_cast<NS>(document->values[0]);
    CHECK((wrapped != nullptr) == (text == "reverse x y y;"));
    const auto* child = wrapped != nullptr
                            ? wrapped->value
                            : cursiv::ast_cast<RC>(document->values[0]);
    CHECK(child != nullptr);
    if (child != nullptr) {
      CHECK(child->y == (text != "reverse x;"));
      CHECK(child->parent == (wrapped != nullptr
                                  ? static_cast<const cursiv::AstNode*>(wrapped)
                                  : document));
    }
    if (wrapped != nullptr) CHECK(wrapped->y && wrapped->parent == document);
  }
}

}  // namespace

int main() {
  const auto ll = tllprefix::generated::Ll::create_parser();
  const auto lr = tllprefix::generated::Lr::create_parser();
  outer_choice_keeps_its_priority(*ll);
  outer_choice_keeps_its_priority(*lr);
  for (const std::string_view text :
       {"name", "name!", "name?", "name!?", "name$", "name!$", "name?$",
        "name!?$", "name%", "name&", "name!?$%", "name!?$&",
        "one two! three? four!?$ five% six!?$&", "let a;", "let a!;",
        "let a!; b? let c;"}) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.accepted && b.accepted);
    CHECK(a.diagnostics.empty() && b.diagnostics.empty());
    const auto* x = a.value_as<tllprefix::Document>();
    const auto* y = b.value_as<tllprefix::Document>();
    CHECK(x != nullptr && y != nullptr);
    if (x == nullptr || y == nullptr) continue;
    CHECK(x->parent == nullptr && y->parent == nullptr);
    CHECK(x->values.size() == y->values.size());
    if (x->values.size() != y->values.size()) continue;
    for (std::size_t i = 0; i < x->values.size(); ++i)
      same_node(x->values[i], y->values[i], x, y, a.cst(), b.cst());
  }
  for (const std::string_view text :
       {"name", "name!", "name?", "one two! three? four", "a! b? c! d? e"}) {
    const std::string source = "[" + std::string(text) + "]";
    const auto a = ll->parse(source);
    const auto b = lr->parse(source);
    CHECK(a.accepted && b.accepted);
    CHECK(a.diagnostics.empty() && b.diagnostics.empty());
    const auto* x = a.value_as<tllprefix::Document>();
    const auto* y = b.value_as<tllprefix::Document>();
    CHECK(x != nullptr && y != nullptr);
    if (x == nullptr || y == nullptr) continue;
    CHECK(x->values.size() == y->values.size());
    if (x->values.size() != y->values.size()) continue;
    for (std::size_t i = 0; i < x->values.size(); ++i)
      same_node(x->values[i], y->values[i], x, y, a.cst(), b.cst());
  }
  return cursiv::test::report("LlPrefixFactor_test");
}
