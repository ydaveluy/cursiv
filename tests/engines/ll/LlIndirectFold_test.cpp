#include <initializer_list>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/IndirectFoldGrammar.hpp"
#include "indirectfoldll/generated/Ll/Module.hpp"

namespace indirect_fold_cases {
namespace {

using namespace tllindirect;

// N carries a name, W is a wrapper, and GL/GT carry leading/trailing glob
// flags. Fixed trees and preorder spans preserve the LR reference without
// building a second generated backend.
std::string shape(const Expression* value, const cursiv::AstNode* parent,
                  const cursiv::Cst& tree, std::string& spans) {
  if (value == nullptr) return "_";
  CHECK(value->parent == parent && value->cstNodeId < tree.size());
  if (value->cstNodeId < tree.size()) {
    const auto& span = tree[value->cstNodeId];
    if (!spans.empty()) spans += ' ';
    spans += std::to_string(span.begin) + '-' + std::to_string(span.end);
    CHECK(span.begin <= span.end && span.end <= tree.text().size());
    if (parent != nullptr && parent->cstNodeId < tree.size()) {
      const auto& outer = tree[parent->cstNodeId];
      CHECK(outer.begin <= span.begin && span.end <= outer.end);
    }
  }
  const auto child = [&](const Expression* node) {
    return shape(node, value, tree, spans);
  };
  if (const auto* node = cursiv::ast_cast<Name>(value))
    return "N(" + std::string(node->text) + ")";
  if (const auto* node = cursiv::ast_cast<Number>(value))
    return std::string(node->text);
  if (const auto* node = cursiv::ast_cast<Binary>(value)) {
    const auto left = child(node->left);
    const auto right = child(node->right);
    return "(" + left + std::string(node->op) + right + ")";
  }
  if (const auto* node = cursiv::ast_cast<Wrapped>(value))
    return "W(" + child(node->value) + ")";
  if (const auto* node = cursiv::ast_cast<Glob>(value))
    return "G" + std::string(node->leading ? "L" : "") +
           std::string(node->trailing ? "T" : "") + "(" + child(node->value) +
           ")";
  CHECK(false);
  return "unknown";
}

struct Case {
  std::string_view text, tree, spans;
};

void expect_tree(const cursiv::Parser& parser, const Case& c) {
  const auto parsed = parser.parse(c.text);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto* document = parsed.value_as<Document>();
  CHECK(document != nullptr && document->parent == nullptr);
  if (document == nullptr) return;
  std::string actual, spans;
  for (const auto* node : document->values) {
    if (!actual.empty()) actual += ' ';
    actual += shape(node, document, parsed.cst(), spans);
  }
  if (actual != c.tree)
    std::printf("indirect fold '%.*s': got %s, expected %.*s\n",
                static_cast<int>(c.text.size()), c.text.data(), actual.c_str(),
                static_cast<int>(c.tree.size()), c.tree.data());
  CHECK(actual == c.tree);
  CHECK(spans == c.spans);
}

void typed_payload_survives_the_rewrite(const cursiv::Parser& parser) {
  const auto arithmetic = parser.parse("1+2*3");
  const auto* doc = arithmetic.value_as<Document>();
  CHECK(doc != nullptr && doc->values.size() == 1);
  if (doc != nullptr && doc->values.size() == 1) {
    const auto* sum = cursiv::ast_cast<Binary>(doc->values[0]);
    CHECK(sum != nullptr && sum->op == "+");
    if (sum != nullptr) {
      const auto* one = cursiv::ast_cast<Number>(sum->left);
      const auto* product = cursiv::ast_cast<Binary>(sum->right);
      CHECK(one != nullptr && one->text == "1");
      CHECK(product != nullptr && product->op == "*");
      if (product != nullptr) {
        const auto* two = cursiv::ast_cast<Number>(product->left);
        const auto* three = cursiv::ast_cast<Number>(product->right);
        CHECK(two != nullptr && two->text == "2");
        CHECK(three != nullptr && three->text == "3");
      }
    }
  }

  for (const std::string_view text : {"id*", "*id"}) {
    const auto result = parser.parse(text);
    const auto* root = result.value_as<Document>();
    CHECK(root != nullptr && root->values.size() == 1);
    if (root == nullptr || root->values.size() != 1) continue;
    const auto* glob = cursiv::ast_cast<Glob>(root->values[0]);
    CHECK(glob != nullptr);
    if (glob == nullptr) continue;
    CHECK(glob->leading == (text == "*id"));
    CHECK(glob->trailing == (text == "id*"));
    const auto* name = cursiv::ast_cast<Name>(glob->value);
    CHECK(name != nullptr && name->text == "id");
  }

  const auto wrapped = parser.parse("1!!");
  const auto* root = wrapped.value_as<Document>();
  CHECK(root != nullptr && root->values.size() == 1);
  if (root == nullptr || root->values.size() != 1) return;
  const auto* outer = cursiv::ast_cast<Wrapped>(root->values[0]);
  CHECK(outer != nullptr);
  const auto* inner =
      outer == nullptr ? nullptr : cursiv::ast_cast<Wrapped>(outer->value);
  CHECK(inner != nullptr);
  const auto* number =
      inner == nullptr ? nullptr : cursiv::ast_cast<Number>(inner->value);
  CHECK(number != nullptr && number->text == "1");
}

void public_entries_keep_their_own_required_suffix(
    const cursiv::Parser& ll, std::string_view prefix,
    std::initializer_list<std::string_view> texts) {
  for (const std::string_view text : texts) {
    const std::string source = std::string(prefix) + std::string(text);
    const auto result = ll.parse(source);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }
}

}  // namespace

void run() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  public_entries_keep_their_own_required_suffix(
      *ll, "primary:", {"1+2", "1*2", "1*2+3"});
  public_entries_keep_their_own_required_suffix(
      *ll, "wrapper:", {"1", "id", "1+2", "(1+2)"});
  typed_payload_survives_the_rewrite(*ll);
  constexpr Case cases[] = {
      {"1", "1", "0-1"},
      {"1!", "W(1)", "0-2 0-1"},
      {"1!!", "W(W(1))", "0-3 0-2 0-1"},
      {"1+2*3", "(1+(2*3))", "0-5 0-1 2-5 2-3 4-5"},
      {"1*2+3", "((1*2)+3)", "0-5 0-3 0-1 2-3 4-5"},
      {"1!+2!", "(W(1)+W(2))", "0-5 0-2 0-1 3-5 3-4"},
      {"1*2!+3!", "(1*W((W(2)+3)))", "0-7 0-1 2-7 2-6 2-4 2-3 5-6"},
      {"(1+2)*3", "((1+2)*3)", "0-7 0-5 1-2 3-4 6-7"},
      {"(1+2)*3!", "((1+2)*W(3))", "0-8 0-5 1-2 3-4 6-8 6-7"},
      {"((1+2!))!!", "W(W((1+W(2))))", "0-10 0-9 0-8 2-3 4-6 4-5"},
      {"id*", "GT(N(id))", "0-3 0-2"},
      {"*id", "GL(N(id))", "0-3 1-3"},
      {"*id*", "GL(GT(N(id)))", "0-4 1-4 1-3"},
      {"*(1+2)*", "GL(GT((1+2)))", "0-7 1-7 1-6 2-3 4-5"},
      {"*", "GL(_)", "0-1"},
      {"id* other", "(N(id)*N(other))", "0-9 0-2 4-9"},
      {"1+2*3 other", "(1+(2*3)) N(other)", "0-5 0-1 2-5 2-3 4-5 6-11"},
      {"1! other", "W(1) N(other)", "0-2 0-1 3-8"},
      {"1*+2!", "(GT(1)+W(2))", "0-5 0-2 0-1 3-5 3-4"},
      {"1*2*3", "(1*GT(2)) 3", "0-4 0-1 2-4 2-3 4-5"},
      {"1*2*", "(1*GT(2))", "0-4 0-1 2-4 2-3"},
      {"*1*2*", "GL((1*GT(2)))", "0-5 1-5 1-2 3-5 3-4"},
      {"**1", "GL(GL(1))", "0-3 1-3 2-3"},
      {"1*2**", "(1*GT(GT(2)))", "0-5 0-1 2-5 2-4 2-3"},
      {"1*2*3*", "(1*GT((2*3)))", "0-6 0-1 2-6 2-5 2-3 4-5"},
      {"1*2*3*4", "(1*GT((2*3))) 4", "0-6 0-1 2-6 2-5 2-3 4-5 6-7"},
      {"primary:1", "1", "8-9"},
      {"primary:1!", "W(1)", "8-10 8-9"},
      {"primary:1!!", "W(W(1))", "8-11 8-10 8-9"},
      {"primary:1+2*", "GT((1+2))", "8-12 8-11 8-9 10-11"},
      {"primary:*1+2", "GL((1+2))", "8-12 9-12 9-10 11-12"},
      {"primary:1*2+3*", "GT(((1*2)+3))", "8-14 8-13 8-11 8-9 10-11 12-13"},
      {"primary:1*2+3!", "W(((1*2)+3))", "8-14 8-13 8-11 8-9 10-11 12-13"},
      {"primary:(1+2)", "(1+2)", "8-13 9-10 11-12"},
      {"primary:(1+2)!", "W((1+2))", "8-14 8-13 9-10 11-12"},
      {"wrapper:id*", "GT(N(id))", "8-11 8-10"},
      {"wrapper:*id", "GL(N(id))", "8-11 9-11"},
      {"wrapper:*", "GL(_)", "8-9"},
      {"wrapper:1+2*", "GT((1+2))", "8-12 8-11 8-9 10-11"},
      {"wrapper:*1+2", "GL((1+2))", "8-12 9-12 9-10 11-12"},
      {"wrapper:1*2+3*", "GT(((1*2)+3))", "8-14 8-13 8-11 8-9 10-11 12-13"},
      {"wrapper:*(1+2)*", "GL(GT((1+2)))", "8-15 9-15 9-14 10-11 12-13"},
  };
  for (const auto& c : cases) expect_tree(*ll, c);
  for (const std::string_view text :
       {"", "!", "1+", "1!+!2", "1((", "(1!", "1*+!2", "((()))"}) {
    const auto result = ll->parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
    for (cursiv::NodeId i = 0; i < result.cst().size(); ++i) {
      const auto& node = result.cst()[i];
      CHECK(node.begin <= node.end && node.end <= text.size());
    }
  }
}

}  // namespace indirect_fold_cases
