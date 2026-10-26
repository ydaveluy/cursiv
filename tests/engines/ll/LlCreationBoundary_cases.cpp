#include <cstdio>
#include <utility>
#include <string>
#include <string_view>
#include <unordered_set>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/CreationBoundaryGrammar.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace ll_creation_boundary_cases {

namespace {

void check_source(const cursiv::ParseResult& result,
                  const cursiv::AstNode& node) {
  CHECK(node.cstNodeId != cursiv::kNoNode &&
        node.cstNodeId < result.cst().size());
  if (node.cstNodeId == cursiv::kNoNode ||
      node.cstNodeId >= result.cst().size())
    return;
  const auto& source = result.cst()[node.cstNodeId];
  CHECK(source.begin <= source.end && source.end <= result.cst().text().size());
}

std::string describe(const cursiv::ParseResult& result,
                     const tllcreation::Node* node,
                     const cursiv::AstNode* parent,
                     std::unordered_set<const cursiv::AstNode*>& seen) {
  if (node == nullptr) return "null";
  const bool fresh = seen.insert(node).second;
  CHECK(fresh);
  if (!fresh) return "duplicate";
  CHECK(node->parent == parent);
  check_source(result, *node);
  std::string out = "(" + std::string(node->name) + ";";
  out += describe(result, node->value, node, seen);
  for (const auto* child : node->children) {
    CHECK(child != nullptr);
    out += describe(result, child, node, seen);
  }
  if (const auto* other = node->other) {
    const bool freshOther = seen.insert(other).second;
    CHECK(freshOther && other->parent == node);
    if (!freshOther) return "duplicate";
    check_source(result, *other);
    out += "other=" + std::string(other->name);
    out += describe(result, other->child, other, seen);
  }
  return out + ")";
}

void expect_tree(const cursiv::Parser& parser, std::string_view text,
                 std::string_view expected) {
  const std::string source = "creation:" + std::string(text);
  const auto parsed = parser.parse(source);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto* root = parsed.value_as<tllcreation::Node>();
  CHECK(root != nullptr);
  if (!root) return;
  std::unordered_set<const cursiv::AstNode*> nodes;
  const auto got = describe(parsed, root, nullptr, nodes);
  if (got != expected)
    std::printf("%.*s: want %.*s, got %s\n", int(text.size()), text.data(),
                int(expected.size()), expected.data(), got.c_str());
  CHECK(got == expected);
}

}  // namespace

void run() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const auto& [text, tree] : {
         std::pair{"root list[]", "(root;null(;null))"},
         {"root list[,]", "(root;null(;null)(;null))"},
         {"root list[,,]", "(root;null(;null)(;null)(;null))"},
         {"root list[a,]", "(root;null(a;null)(;null))"},
         {"root list[,b]", "(root;null(;null)(b;null))"},
         {"root list[a,,b]", "(root;null(a;null)(;null)(b;null))"},
         {"root single[]", "(root;(;null))"},
         {"root single[a]", "(root;(a;null))"},
         {"root optional[]", "(root;null)"},
         {"root optional[!]", "(root;(;null))"},
         {"root optional[!a]", "(root;(a;null))"},
         {"root other[:]", "(root;nullother=(;null))"},
         {"root other[a:]", "(root;nullother=a(;null))"},
         {"root other[:b]", "(root;nullother=(b;null))"},
         {"root other[a:b]", "(root;nullother=a(b;null))"},
         {"tree:;", "(;null)"},
         {"tree:A;", "(A;null)"},
         {"tree:(A);", "(;null(A;null))"},
         {"tree:(,);", "(;null(;null)(;null))"},
         {"tree:(,,(,));", "(;null(;null)(;null)(;null(;null)(;null)))"},
         {"tree:(A,,B)R;", "(R;null(A;null)(;null)(B;null))"}})
    expect_tree(*ll, text, tree);
}

}  // namespace ll_creation_boundary_cases
