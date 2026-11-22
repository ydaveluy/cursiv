#include "backwardlr/generated/Backward/Module.hpp"
#include "asttreelr/generated/Combined/Module.hpp"
#include "asttreeaux/generated/Aux/Module.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/AstTreeGrammars.hpp"
#include "engines/shared/Checks.hpp"
#include "support/AstText.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ast_tree_cases {

namespace {

bool read(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() &&
         r.error == cursiv::AstBuildError::None && r.value != nullptr;
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  namespace ast = ttree::ast;
  cursiv::test::countExpectations();

  const auto parser = g::Combined::createParser();

  for (const auto source : {"postfix:x!?", "postfix:x ?!", "postfix:x!"}) {
    const auto result = parser->parse(source);
    const auto* node = exactly<ast::Postfix>(result);
    CHECK(read(result) && node && node->child && node->child->parent == node);
    if (node && node->child) {
      CHECK(node->marked == std::string_view(source).ends_with("!?"));
      CHECK(node->child->marked == std::string_view(source).ends_with("?!"));
    }
  }
  CHECK(parser->getGrammar().spellingDoc("!") == "Wraps the preceding expression.");
  CHECK(!read(parser->parse("postfix:x! ?")));
  const auto tied = parser->parse("tied:x!!");
  const auto* outer = exactly<ast::Postfix>(tied);
  CHECK(read(tied) && outer && !outer->marked && outer->child &&
        outer->child->marked && outer->child->child);
  const auto shorter = parser->parse("short:x!!");
  outer = exactly<ast::Postfix>(shorter);
  CHECK(read(shorter) && outer && outer->marked && outer->child);

  const auto empty = parser->parse("quick:@END@");
  const auto* module = exactly<ast::Module>(empty);
  CHECK(read(empty) && module != nullptr && module->variables.empty());
  const auto some = parser->parse("quick:let count = 12; // note\nlet b = 3;@END@");
  module = exactly<ast::Module>(some);
  CHECK(read(some) && module != nullptr && module->variables.size() == 2);
  if (module != nullptr && module->variables.size() == 2) {
    CHECK(module->variables[0]->name == "count" &&
          module->variables[0]->value == 12);
    CHECK(module->variables[1]->name == "b" &&
          module->variables[1]->value == 3);
  }

  const auto declared = parser->parse("declared:f ()");
  const auto* function = exactly<ast::Function>(declared);
  CHECK(read(declared) && function != nullptr && function->name == "f");

  const auto exported = parser->parse("exported:export fn f");
  const auto* fn = exactly<ast::Fn>(exported);
  CHECK(read(exported) && fn != nullptr && fn->name == "f" && fn->exported);
  const auto plain = parser->parse("exported:fn f");
  fn = exactly<ast::Fn>(plain);
  CHECK(read(plain) && fn != nullptr && fn->name == "f" && !fn->exported);
  const auto variable = parser->parse("exported:export var v = 7");
  const auto* var = exactly<ast::Var>(variable);
  CHECK(read(variable) && var != nullptr && var->name == "v" &&
        var->value == 7 && var->exported &&
        exactly<ast::Fn>(variable) == nullptr);

  const auto name = parser->parse("indexed:a");
  const auto* leaf = exactly<ast::Name>(name);
  CHECK(read(name) && leaf != nullptr && leaf->name == "a" &&
        exactly<ast::Index>(name) == nullptr);
  const auto one = parser->parse("indexed:a[0]");
  const auto* index = exactly<ast::Index>(one);
  CHECK(read(one) && index != nullptr);
  if (index != nullptr) {
    const auto* array = static_cast<const ast::Name*>(index->array);
    const auto* at = static_cast<const ast::Number*>(index->index);
    CHECK(array != nullptr && array->name == "a" && at != nullptr &&
          at->value == 0);
  }
  const auto two = parser->parse("indexed:a[b][1]");
  index = exactly<ast::Index>(two);
  CHECK(read(two) && index != nullptr);
  if (index != nullptr) {
    const auto* inner = static_cast<const ast::Index*>(index->array);
    CHECK(inner != nullptr && inner->array != nullptr &&
          static_cast<const ast::Name*>(inner->array)->name == "a");
    CHECK(static_cast<const ast::Number*>(index->index)->value == 1);
  }

  const auto apart = parser->parse("apart:e");
  const auto* a = exactly<ast::A>(apart);
  CHECK(read(apart) && a != nullptr && a->m != nullptr && a->m->n == "e" &&
        a->items.empty());

  const auto perturn = parser->parse("perturn:x y");
  a = exactly<ast::A>(perturn);
  CHECK(read(perturn) && a != nullptr && a->m == nullptr &&
        a->items.size() == 2);
  if (a != nullptr && a->items.size() == 2)
    CHECK(a->items[0]->n == "x" && a->items[1]->n == "y");

  const auto presentEmpty = parser->parse("present:");
  a = exactly<ast::A>(presentEmpty);
  CHECK(read(presentEmpty) && a != nullptr && a->m != nullptr &&
        a->m->n.empty());
  const auto presentFull = parser->parse("present:x");
  a = exactly<ast::A>(presentFull);
  CHECK(read(presentFull) && a != nullptr && a->m != nullptr &&
        a->m->n == "x");

  const auto absentEmpty = parser->parse("absent:");
  a = exactly<ast::A>(absentEmpty);
  CHECK(read(absentEmpty) && a != nullptr && a->m == nullptr);
  const auto absentFull = parser->parse("absent:x");
  a = exactly<ast::A>(absentFull);
  CHECK(read(absentFull) && a != nullptr && a->m != nullptr &&
        a->m->n == "x");

  const auto nested = parser->parse("nested:x");
  const auto* e = exactly<ast::E>(nested);
  CHECK(read(nested) && e != nullptr && e->d != nullptr &&
        e->d->c != nullptr && e->d->c->n == "x");
  CHECK(read(nested) && nested.nodes().size() == 3);

  const auto adopted = parser->parse("adopted:$ x");
  const auto* pair = exactly<ast::Pair>(adopted);
  CHECK(read(adopted) && pair != nullptr && pair->leaf != nullptr &&
        pair->leaf->name == "x");
  CHECK(read(adopted) && adopted.nodes().size() == 2);

  const auto chained = parser->parse("chained:a.b c");
  const auto* block = exactly<ast::Block>(chained);
  CHECK(read(chained) && block != nullptr && block->atoms.size() == 2);
  if (block != nullptr && block->atoms.size() == 2) {
    const auto* selected = static_cast<const ast::Index*>(block->atoms[0]);
    CHECK(block->atoms[0]->type == &cursiv::kAstTypeTag<ast::Index> &&
          selected->array != nullptr && selected->index != nullptr);
    CHECK(block->atoms[1]->type == &cursiv::kAstTypeTag<ast::Name>);
  }
}

}  // namespace ast_tree_cases

namespace member_order_cases {

// A class's members are visited and described in the order the class declares
// them, a base's first, whichever order the grammar's arms name them in.

namespace {

using tmember::ast::Base;
using tmember::ast::Derived;
using tmember::ast::Unnamed;

struct Engine {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
  std::string_view prefix;
};

std::string tree(const Engine& e, std::string_view text) {
  const std::string source = std::string(e.prefix) + std::string(text);
  const cursiv::ParseResult r = e.parser->parse(source);
  std::string out;
  if (!r.accepted || !r.diagnostics.empty() || r.value == nullptr)
    return "<refused>";
  support::ast_of(*e.reflection, *r.value, out);
  return out;
}

std::string properties(const Engine& e, const cursiv::AstTypeTag* type) {
  std::string out;
  for (const auto& p : e.reflection->getTypeMetaData(type).properties)
    out += std::string(p.name) + ' ';
  return out;
}

void members_are_visited_in_declaration_order(const Engine& e) {
  constexpr std::string_view declared =
      "Derived{first=Leaf{} second=Leaf{} third=Leaf{} items0=Leaf{} "
      "items1=Leaf{} }";
  CHECK(tree(e, "f a l z s b t c i d i e") == declared);
  CHECK(tree(e, "i d i e t c s b l z f a") == declared);
  CHECK(tree(e, "t c f a i d i e s b l z") == declared);
}

void properties_follow_the_class(const Engine& e) {
  CHECK(properties(e, &cursiv::kAstTypeTag<Base>) == "first link second ");
  CHECK(properties(e, &cursiv::kAstTypeTag<Derived>) ==
        "first link second third items ");
}

void a_type_the_grammar_does_not_name_is_visited_the_same_way(const Engine& e) {
  tmember::ast::Leaf a, b, c;
  Unnamed node;
  node.type = &cursiv::kAstTypeTag<Unnamed>;
  node.third = &c;
  node.second = &b;
  node.first = &a;
  std::string seen;
  e.reflection->visitProperties(
      node, [&](std::string_view property, std::uint32_t, cursiv::AstNode&) {
        seen += std::string(property) + ' ';
      });
  CHECK(seen == "first second third ");
}

void every_engine(const Engine& e) {
  members_are_visited_in_declaration_order(e);
  properties_follow_the_class(e);
  a_type_the_grammar_does_not_name_is_visited_the_same_way(e);
}

}  // namespace

void run() {
  every_engine({ttree::generated::Combined::createParser(),
                ttree::generated::Combined::createAstReflection(), "members:"});
  // One reversed grammar challenges the metadata emitter's encounter order.
  every_engine({tmember::generated::Backward::createParser(),
                tmember::generated::Backward::createAstReflection(), ""});
}

}  // namespace member_order_cases

namespace cyclic_adopt_cases {

using namespace tcyclicadopt;

static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<Grammar::Expression>, Element>);
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<Grammar::Sequence>, Element>);
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<Grammar::Token>, Element>);
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<Grammar::Primary>, Element>);
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<Grammar::Parent>, Element>);
static_assert(
    !cursiv::detail::Normalize<Grammar>::rewritten<Grammar::Expression>);

namespace {

std::string tree(const Base* node, const cursiv::AstNode* parent,
                 std::unordered_set<const cursiv::AstNode*>& reached) {
  CHECK(node && node->parent == parent);
  if (!node || !reached.insert(node).second) return "?";
  const auto* element = cursiv::ast_cast<Element>(node);
  CHECK(element);
  if (!element) return "?";
  std::string out;
  if (const auto* leaf = cursiv::ast_cast<Leaf>(node)) {
    CHECK(leaf->value == "a");
    out = leaf->value;
  } else if (const auto* counted = cursiv::ast_cast<Counted>(node)) {
    out = tree(counted->element, node, reached) +
          std::string(counted->cardinality);
  } else {
    const auto* group = cursiv::ast_cast<Group>(node);
    const auto* alternatives = cursiv::ast_cast<Alternatives>(node);
    CHECK(group || alternatives);
    if (!group && !alternatives) return "?";
    const auto& children = group ? group->elements : alternatives->elements;
    CHECK(children.size() >= 2);
    out = group ? "G[" : "A[";
    for (const auto* child : children) out += tree(child, node, reached);
    out += "]";
  }
  if (element->parenthesized) out = "(" + out + ")";
  return out;
}

void verify(const cursiv::Parser& parser) {
  for (const auto& [source, expected] : {std::pair{"a", "a"},
                                         {"a+", "a+"},
                                         {"(a)", "(a)"},
                                         {"((a))", "(a)"},
                                         {"aa", "G[aa]"},
                                         {"a|a|a", "A[aaa]"},
                                         {"(aa)+", "(G[aa])+"},
                                         {"a+(a|a)", "G[a+(A[aa])]"},
                                         {"a|((aa))+|a", "A[a(G[aa])+a]"}}) {
    const std::string input = "cyclic:" + std::string(source);
    const auto result = parser.parse(input);
    CHECK(result.accepted && !result.cst().recovered() &&
          result.diagnostics.empty());
    const auto* root = cursiv::ast_cast<Base>(result.value);
    std::unordered_set<const cursiv::AstNode*> reached;
    const auto actual = tree(root, nullptr, reached);
    if (actual != expected)
      std::fprintf(stderr, "%s: got %s; expected %s\n", source, actual.c_str(),
                   expected);
    CHECK(actual == expected);
    CHECK(reached.size() == result.nodes().size());
    for (const auto* node : result.nodes()) CHECK(reached.contains(node));
  }
  for (std::string_view source : {"", "+", "()", "a|", "(a", "a++"}) {
    const std::string input = "cyclic:" + std::string(source);
    const auto result = parser.parse(input);
    CHECK(!result.accepted || result.cst().recovered() ||
          !result.diagnostics.empty());
  }
}

}  // namespace

void run() {
  const auto lr = ttree::generated::Combined::createParser();
  const auto aux = ttree::generated::Aux::createParser();
  verify(*lr);
  verify(*aux);
}

}  // namespace cyclic_adopt_cases

namespace nested_run_cases {

namespace {

namespace ast = tnestedrun::ast;

const ast::Groups* read(const cursiv::ParseResult& r) {
  if (!r.accepted || !r.diagnostics.empty() ||
      r.error != cursiv::AstBuildError::None || r.value == nullptr ||
      r.value->type != &cursiv::kAstTypeTag<ast::Groups>)
    return nullptr;
  return static_cast<const ast::Groups*>(r.value);
}

std::vector<std::size_t> sizes(const ast::Groups& groups) {
  std::vector<std::size_t> out;
  for (const ast::Group* g : groups.groups) out.push_back(g->xs.size());
  return out;
}

using Sizes = std::vector<std::size_t>;

void groups(const cursiv::Parser& parser, std::string_view prefix) {
  const std::string emptySource(prefix);
  const std::string runSource = std::string(prefix) + "x x x";
  const std::string cutSource = std::string(prefix) + "x x ; x";
  const std::string eachSource = std::string(prefix) + "x ; x ; x x";

  const auto empty = parser.parse(emptySource);
  const auto* none = read(empty);
  CHECK(none != nullptr && none->groups.empty());

  const auto run = parser.parse(runSource);
  const auto* one = read(run);
  CHECK(one != nullptr && sizes(*one) == Sizes{3});

  const auto cut = parser.parse(cutSource);
  const auto* two = read(cut);
  CHECK(two != nullptr && sizes(*two) == Sizes{2, 1});

  const auto each = parser.parse(eachSource);
  const auto* three = read(each);
  CHECK(three != nullptr && sizes(*three) == Sizes{1, 1, 2});
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  cursiv::test::countExpectations();

  const auto parser = g::Combined::createParser();
  const auto empty = parser->parse("run:");
  const auto* none = read(empty);
  CHECK(none != nullptr && none->items.empty());
  const auto run = parser->parse("run:x x x x");
  const auto* items = read(run);
  CHECK(items != nullptr && items->items.size() == 4);

  const auto wrapped = parser->parse("run:wrap:x x x");
  const auto* group = read(wrapped);
  CHECK(group && group->items.size() == 3);
  if (group && group->items.size() == 3) {
    CHECK(group->items[0]->cstNodeId < group->items[1]->cstNodeId);
    CHECK(group->items[1]->cstNodeId < group->items[2]->cstNodeId);
  }

  groups(*parser, "run:inplace:");
  groups(*parser, "run:behind:");
}

}  // namespace nested_run_cases

namespace ast_materialise_cases { void run(); }

namespace nest_adopt_cases { void run(); }

namespace directive_sequence_cases { void run(); }

namespace data_parts_cases { void run(); }

namespace flag_operand_cases { void run(); }

int main() {
  cursiv::test::countExpectations();
  ast_tree_cases::run();
  member_order_cases::run();
  cyclic_adopt_cases::run();
  nested_run_cases::run();
  ast_materialise_cases::run();
  nest_adopt_cases::run();
  directive_sequence_cases::run();
  data_parts_cases::run();
  flag_operand_cases::run();
  return cursiv::test::report("AstTree_test");
}
