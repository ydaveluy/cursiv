#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/PrefixFactorGrammar.hpp"
#include "prefixfactorll/generated/Ll/Module.hpp"

namespace {

using namespace tllprefix;

// Fixed shapes and preorder spans exercise the LL prefix rewrite without
// building another backend. Shared conformance suites compare both engines.
std::string node_shape(const Expression* value, const cursiv::AstNode* parent,
                       const cursiv::Cst& tree, std::string& spans) {
  CHECK(value != nullptr);
  if (value == nullptr) return "null";
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
    return node_shape(node, value, tree, spans);
  };
  if (const auto* node = cursiv::ast_cast<Name>(value))
    return "Name(" + std::string(node->text) + ")";
  if (const auto* node = cursiv::ast_cast<Unary>(value)) {
    CHECK(node->enabled);
    return "Unary(" + child(node->value) + ")";
  }
  if (const auto* node = cursiv::ast_cast<Wrapped>(value))
    return "Wrapped(" + child(node->value) + ")";
  if (const auto* node = cursiv::ast_cast<Bag>(value)) {
    CHECK(node->values.size() == 1);
    return "Bag(" + (node->values.empty() ? "null" : child(node->values[0])) +
           ")";
  }
  if (const auto* node = cursiv::ast_cast<LeftTag>(value)) {
    CHECK(node->enabled);
    return "LeftTag(" + child(node->operand) + ")";
  }
  if (const auto* node = cursiv::ast_cast<RightTag>(value)) {
    CHECK(node->enabled && node->parts.size() == 1);
    return "RightTag(" +
           (node->parts.empty() ? "null" : child(node->parts[0])) + ")";
  }
  if (const auto* node = cursiv::ast_cast<Let>(value))
    return "Let(" + child(node->value) + ")";
  CHECK(false);
  return "unknown";
}

void expect_shape(const cursiv::Parser& parser, std::string_view source,
                  std::string_view expected, std::string_view expectedSpans) {
  const auto parsed = parser.parse(source);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<Document>();
  CHECK(doc != nullptr && doc->parent == nullptr);
  if (doc == nullptr) return;
  std::string shape, spans;
  for (const auto* node : doc->values) {
    if (!shape.empty()) shape += ' ';
    shape += node_shape(node, doc, parsed.cst(), spans);
  }
  CHECK(shape == expected);
  CHECK(spans == expectedSpans);
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
  outer_choice_keeps_its_priority(*ll);
  struct Case {
    std::string_view text, shape, spans;
  };
  constexpr Case cases[] = {
      {"name", "Name(name)", "0-4"},
      {"name!", "Unary(Name(name))", "0-5 0-4"},
      {"name?", "Wrapped(Name(name))", "0-5 0-4"},
      {"name!?", "Wrapped(Unary(Name(name)))", "0-6 0-5 0-4"},
      {"name$", "Bag(Name(name))", "0-5 0-4"},
      {"name!$", "Bag(Unary(Name(name)))", "0-6 0-5 0-4"},
      {"name?$", "Bag(Wrapped(Name(name)))", "0-6 0-5 0-4"},
      {"name!?$", "Bag(Wrapped(Unary(Name(name))))", "0-7 0-6 0-5 0-4"},
      {"name%", "LeftTag(Name(name))", "0-5 0-4"},
      {"name&", "RightTag(Name(name))", "0-5 0-4"},
      {"name!?$%", "LeftTag(Bag(Wrapped(Unary(Name(name)))))",
       "0-8 0-7 0-6 0-5 0-4"},
      {"name!?$&", "RightTag(Bag(Wrapped(Unary(Name(name)))))",
       "0-8 0-7 0-6 0-5 0-4"},
      {"one two! three? four!?$ five% six!?$&",
       "Name(one) Unary(Name(two)) Wrapped(Name(three)) "
       "Bag(Wrapped(Unary(Name(four)))) LeftTag(Name(five)) "
       "RightTag(Bag(Wrapped(Unary(Name(six)))))",
       "0-3 4-8 4-7 9-15 9-14 16-23 16-22 16-21 16-20 24-29 24-28 30-37 30-36 "
       "30-35 30-34 30-33"},
      {"let a;", "Let(Name(a))", "0-6 4-5"},
      {"let a!;", "Let(Unary(Name(a)))", "0-7 4-6 4-5"},
      {"let a!; b? let c;", "Let(Unary(Name(a))) Wrapped(Name(b)) Let(Name(c))",
       "0-7 4-6 4-5 8-10 8-9 11-17 15-16"},
  };
  for (const auto& c : cases) expect_shape(*ll, c.text, c.shape, c.spans);
  constexpr Case appended[] = {
      {"name", "Name(name)", "1-5"},
      {"name!", "Unary(Name(name))", "1-6 1-5"},
      {"name?", "Wrapped(Name(name))", "1-6 1-5"},
      {"one two! three? four",
       "Name(one) Unary(Name(two)) Wrapped(Name(three)) Name(four)",
       "1-4 5-9 5-8 10-16 10-15 17-21"},
      {"a! b? c! d? e",
       "Unary(Name(a)) Wrapped(Name(b)) Unary(Name(c)) Wrapped(Name(d)) "
       "Name(e)",
       "1-3 1-2 4-6 4-5 7-9 7-8 10-12 10-11 13-14"},
  };
  for (const auto& c : appended) {
    const std::string source = "[" + std::string(c.text) + "]";
    expect_shape(*ll, source, c.shape, c.spans);
  }
  return cursiv::test::report("LlPrefixFactor_test");
}
