#include <initializer_list>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/IndirectFoldGrammar.hpp"
#include "indirectfoldll/generated/Ll/Module.hpp"
#include "indirectfoldlr/generated/Lr/Module.hpp"

namespace {

using namespace tllindirect;

void same_payload(const Expression* a, const Expression* b,
                  std::string_view input, const cursiv::Cst& ca,
                  const cursiv::Cst& cb) {
  CHECK((a == nullptr) == (b == nullptr));
  if (a == nullptr || b == nullptr) return;
  CHECK(a->type == b->type);
  if (a->type != b->type) return;
  CHECK(a->cstNodeId < ca.size() && b->cstNodeId < cb.size());
  if (a->cstNodeId < ca.size() && b->cstNodeId < cb.size()) {
    const auto& x = ca[a->cstNodeId];
    const auto& y = cb[b->cstNodeId];
    CHECK(x.begin == y.begin && x.end == y.end);
  }
  if (const auto* x = cursiv::ast_cast<Name>(a)) {
    CHECK(x->text == cursiv::ast_cast<Name>(b)->text);
  } else if (const auto* x = cursiv::ast_cast<Number>(a)) {
    CHECK(x->text == cursiv::ast_cast<Number>(b)->text);
  } else if (const auto* x = cursiv::ast_cast<Binary>(a)) {
    const auto* y = cursiv::ast_cast<Binary>(b);
    CHECK(x->op == y->op);
    same_payload(x->left, y->left, input, ca, cb);
    same_payload(x->right, y->right, input, ca, cb);
  } else if (const auto* x = cursiv::ast_cast<Wrapped>(a)) {
    same_payload(x->value, cursiv::ast_cast<Wrapped>(b)->value, input, ca, cb);
  } else if (const auto* x = cursiv::ast_cast<Glob>(a)) {
    const auto* y = cursiv::ast_cast<Glob>(b);
    if (x->leading != y->leading || x->trailing != y->trailing)
      std::printf("glob flags for '%.*s': LL=%d/%d LR=%d/%d\n",
                  int(input.size()), input.data(), x->leading, x->trailing,
                  y->leading, y->trailing);
    CHECK(x->leading == y->leading && x->trailing == y->trailing);
    same_payload(x->value, y->value, input, ca, cb);
  }
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

void public_entries_keep_the_complete_expression(
    const cursiv::Parser& ll, const cursiv::Parser& lr,
    std::string_view prefix, std::initializer_list<std::string_view> texts) {
  for (const std::string_view text : texts) {
    const std::string source = std::string(prefix) + std::string(text);
    const auto a = ll.parse(source), b = lr.parse(source);
    if (!a.diagnostics.empty() || !b.diagnostics.empty())
      std::printf("external entry '%.*s': LL diagnostics=%zu, LR=%zu\n",
                  int(text.size()), text.data(), a.diagnostics.size(),
                  b.diagnostics.size());
    CHECK(a.accepted && b.accepted);
    CHECK(a.diagnostics.empty() && b.diagnostics.empty());
    const auto* x = a.value_as<Document>();
    const auto* y = b.value_as<Document>();
    CHECK(x && y && x->values.size() == 1 && y->values.size() == 1);
    if (!x || !y || x->values.size() != 1 || y->values.size() != 1) continue;
    same_payload(x->values[0], y->values[0], text, a.cst(), b.cst());
    if (text == "1+2*") {
      for (const auto* doc : {x, y}) {
        const auto* glob = cursiv::ast_cast<Glob>(doc->values[0]);
        CHECK(glob && glob->trailing && !glob->leading);
        const auto* sum =
            glob ? cursiv::ast_cast<Binary>(glob->value) : nullptr;
        CHECK(sum && sum->op == "+");
        const auto* one = sum ? cursiv::ast_cast<Number>(sum->left) : nullptr;
        const auto* two = sum ? cursiv::ast_cast<Number>(sum->right) : nullptr;
        CHECK(one && one->text == "1" && two && two->text == "2");
      }
    }
  }
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

int main() {
  const auto ll = tllindirect::generated::Ll::create_parser();
  const auto lr = tllindirect::generated::Lr::create_parser();
  const auto llReflection = tllindirect::generated::Ll::create_ast_reflection();
  const auto lrReflection = tllindirect::generated::Lr::create_ast_reflection();
  public_entries_keep_the_complete_expression(
      *ll, *lr, "primary:",
      {"1", "1!", "1!!", "1+2*", "*1+2", "1*2+3*", "1*2+3!", "(1+2)",
       "(1+2)!"});
  public_entries_keep_the_complete_expression(
      *ll, *lr, "wrapper:",
      {"id*", "*id", "*", "1+2*", "*1+2", "1*2+3*", "*(1+2)*"});
  public_entries_keep_their_own_required_suffix(
      *ll, "primary:", {"1+2", "1*2", "1*2+3"});
  public_entries_keep_their_own_required_suffix(
      *ll, "wrapper:", {"1", "id", "1+2", "(1+2)"});
  typed_payload_survives_the_rewrite(*ll);
  typed_payload_survives_the_rewrite(*lr);
  for (const std::string_view text :
       {"1", "1!", "1!!", "1+2*3", "1*2+3", "1!+2!", "1*2!+3!", "(1+2)*3",
        "(1+2)*3!", "((1+2!))!!", "id*", "*id", "*id*", "*(1+2)*", "*",
        "id* other", "1+2*3 other", "1! other", "1*+2!", "1*2*3", "1*2*",
        "*1*2*", "**1", "1*2**", "1*2*3*", "1*2*3*4"}) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.accepted && b.accepted);
    CHECK(a.diagnostics.empty() && b.diagnostics.empty());
    CHECK(a.value != nullptr && b.value != nullptr);
    if (a.value == nullptr || b.value == nullptr) continue;
    std::string x, y;
    cursiv::test::ast_of(*llReflection, *a.value, x);
    cursiv::test::ast_of(*lrReflection, *b.value, y);
    CHECK(x == y);
    const auto* docA = a.value_as<tllindirect::Document>();
    const auto* docB = b.value_as<tllindirect::Document>();
    CHECK(docA != nullptr && docB != nullptr);
    if (docA == nullptr || docB == nullptr) continue;
    CHECK(docA->values.size() == docB->values.size());
    if (docA->values.size() != docB->values.size()) continue;
    for (std::size_t i = 0; i < docA->values.size(); ++i)
      same_payload(docA->values[i], docB->values[i], text, a.cst(), b.cst());
  }
  for (const std::string_view text :
       {"", "!", "1+", "1!+!2", "1((", "(1!", "1*+!2", "((()))"}) {
    const auto result = ll->parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
    for (cursiv::NodeId i = 0; i < result.cst().size(); ++i) {
      const auto& node = result.cst()[i];
      CHECK(node.begin <= node.end && node.end <= text.size());
    }
  }
  return cursiv::test::report("LlIndirectFold_test");
}
