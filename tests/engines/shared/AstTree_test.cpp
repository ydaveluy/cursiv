#include "backwardlr/generated/BackwardLr/Module.hpp"
#include "asttreell/generated/Ll/Module.hpp"
#include "asttreelr/generated/Lr/Module.hpp"
#include "asttreeaux/generated/Aux/Module.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/AstTreeGrammars.hpp"
#include "engines/shared/Checks.hpp"
#include <array>
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

using Pair = std::array<std::unique_ptr<cursiv::Parser>, 2>;

bool read(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() &&
         r.error == cursiv::AstBuildError::none && r.value != nullptr;
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  namespace ast = ttree::ast;
  cursiv::test::count_expectations();

  const Pair quick{g::Ll::create_parser(), g::Lr::create_parser()};
  for (const auto& parser : quick) {
    const auto empty = parser->parse("quick:@END@");
    const auto* module = exactly<ast::Module>(empty);
    CHECK(read(empty) && module != nullptr && module->variables.empty());
    const auto some = parser->parse("quick:let count = 12; // note\nlet b = 3;@END@");
    module = exactly<ast::Module>(some);
    CHECK(read(some) && module != nullptr && module->variables.size() == 2);
    if (module == nullptr || module->variables.size() != 2) continue;
    CHECK(module->variables[0]->name == "count" &&
          module->variables[0]->value == 12);
    CHECK(module->variables[1]->name == "b" &&
          module->variables[1]->value == 3);
  }

  const Pair declared{g::Ll::create_parser(),
                      g::Lr::create_parser()};
  for (const auto& parser : declared) {
    const auto result = parser->parse("declared:f ()");
    const auto* function = exactly<ast::Function>(result);
    CHECK(read(result) && function != nullptr && function->name == "f");
  }

  const Pair exported{g::Ll::create_parser(),
                      g::Lr::create_parser()};
  for (const auto& parser : exported) {
    const auto result = parser->parse("exported:export fn f");
    const auto* fn = exactly<ast::Fn>(result);
    CHECK(read(result) && fn != nullptr && fn->name == "f" && fn->exported);
    const auto plain = parser->parse("exported:fn f");
    fn = exactly<ast::Fn>(plain);
    CHECK(read(plain) && fn != nullptr && fn->name == "f" && !fn->exported);
    const auto variable = parser->parse("exported:export var v = 7");
    const auto* var = exactly<ast::Var>(variable);
    CHECK(read(variable) && var != nullptr && var->name == "v" &&
          var->value == 7 && var->exported &&
          exactly<ast::Fn>(variable) == nullptr);
  }

  const Pair indexed{g::Ll::create_parser(),
                     g::Lr::create_parser()};
  for (const auto& parser : indexed) {
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
    if (index == nullptr) continue;
    const auto* inner = static_cast<const ast::Index*>(index->array);
    CHECK(inner != nullptr && inner->array != nullptr &&
          static_cast<const ast::Name*>(inner->array)->name == "a");
    CHECK(static_cast<const ast::Number*>(index->index)->value == 1);
  }

  const Pair apart{g::Ll::create_parser(), g::Lr::create_parser()};
  for (const auto& parser : apart) {
    const auto result = parser->parse("apart:e");
    const auto* a = exactly<ast::A>(result);
    CHECK(read(result) && a != nullptr && a->m != nullptr && a->m->n == "e" &&
          a->items.empty());
  }

  const Pair perturn{g::Ll::create_parser(),
                     g::Lr::create_parser()};
  for (const auto& parser : perturn) {
    const auto result = parser->parse("perturn:x y");
    const auto* a = exactly<ast::A>(result);
    CHECK(read(result) && a != nullptr && a->m == nullptr &&
          a->items.size() == 2);
    if (a == nullptr || a->items.size() != 2) continue;
    CHECK(a->items[0]->n == "x" && a->items[1]->n == "y");
  }

  const Pair present{g::Ll::create_parser(),
                     g::Lr::create_parser()};
  for (const auto& parser : present) {
    const auto empty = parser->parse("present:");
    const auto* a = exactly<ast::A>(empty);
    CHECK(read(empty) && a != nullptr && a->m != nullptr && a->m->n.empty());
    const auto full = parser->parse("present:x");
    a = exactly<ast::A>(full);
    CHECK(read(full) && a != nullptr && a->m != nullptr && a->m->n == "x");
  }

  const Pair absent{g::Ll::create_parser(), g::Lr::create_parser()};
  for (const auto& parser : absent) {
    const auto empty = parser->parse("absent:");
    const auto* a = exactly<ast::A>(empty);
    CHECK(read(empty) && a != nullptr && a->m == nullptr);
    const auto full = parser->parse("absent:x");
    a = exactly<ast::A>(full);
    CHECK(read(full) && a != nullptr && a->m != nullptr && a->m->n == "x");
  }

  const Pair nested{g::Ll::create_parser(), g::Lr::create_parser()};
  for (const auto& parser : nested) {
    const auto result = parser->parse("nested:x");
    const auto* e = exactly<ast::E>(result);
    CHECK(read(result) && e != nullptr && e->d != nullptr &&
          e->d->c != nullptr && e->d->c->n == "x");
    CHECK(read(result) && result.nodes.size() == 3);
  }

  const Pair adopted{g::Ll::create_parser(),
                     g::Lr::create_parser()};
  for (const auto& parser : adopted) {
    const auto result = parser->parse("adopted:$ x");
    const auto* pair = exactly<ast::Pair>(result);
    CHECK(read(result) && pair != nullptr && pair->leaf != nullptr &&
          pair->leaf->name == "x");
    CHECK(read(result) && result.nodes.size() == 2);
  }

  const Pair chained{g::Ll::create_parser(),
                     g::Lr::create_parser()};
  for (const auto& parser : chained) {
    const auto result = parser->parse("chained:a.b c");
    const auto* block = exactly<ast::Block>(result);
    CHECK(read(result) && block != nullptr && block->atoms.size() == 2);
    if (block == nullptr || block->atoms.size() != 2) continue;
    const auto* index = static_cast<const ast::Index*>(block->atoms[0]);
    CHECK(block->atoms[0]->type == &cursiv::ast_type_tag<ast::Index> &&
          index->array != nullptr && index->index != nullptr);
    CHECK(block->atoms[1]->type == &cursiv::ast_type_tag<ast::Name>);
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
  cursiv::test::ast_of(*e.reflection, *r.value, out);
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
  CHECK(properties(e, &cursiv::ast_type_tag<Base>) == "first link second ");
  CHECK(properties(e, &cursiv::ast_type_tag<Derived>) == "third items ");
}

void a_type_the_grammar_does_not_name_is_visited_alike(const Engine& e) {
  tmember::ast::Leaf a, b, c;
  Unnamed node;
  node.type = &cursiv::ast_type_tag<Unnamed>;
  node.third = &c;
  node.second = &b;
  node.first = &a;
  std::string seen;
  e.reflection->visitMembers(
      node, [&](std::string_view property, std::uint32_t, cursiv::AstNode&) {
        seen += std::string(property) + ' ';
      });
  CHECK(seen == "first second third ");
}

void every_engine(const Engine& e) {
  members_are_visited_in_declaration_order(e);
  properties_follow_the_class(e);
  a_type_the_grammar_does_not_name_is_visited_alike(e);
}

}  // namespace

void run() {
  every_engine({ttree::generated::Ll::create_parser(),
                ttree::generated::Ll::create_ast_reflection(), "members:"});
  every_engine({ttree::generated::Lr::create_parser(),
                ttree::generated::Lr::create_ast_reflection(), "members:"});
  // Both backends parse every input ordering above. One reversed grammar is
  // enough to challenge their shared metadata emitter's encounter order.
  every_engine({tmember::generated::BackwardLr::create_parser(),
                tmember::generated::BackwardLr::create_ast_reflection(), ""});
}

}  // namespace member_order_cases

namespace cyclic_adopt_cases {

using namespace tcyclicadopt;

static_assert(
    std::is_same_v<cursiv::rule_type_t<Grammar::Expression>, Element>);
static_assert(std::is_same_v<cursiv::rule_type_t<Grammar::Sequence>, Element>);
static_assert(std::is_same_v<cursiv::rule_type_t<Grammar::Token>, Element>);
static_assert(std::is_same_v<cursiv::rule_type_t<Grammar::Primary>, Element>);
static_assert(std::is_same_v<cursiv::rule_type_t<Grammar::Parent>, Element>);
static_assert(!cursiv::Normalize<Grammar>::owed<Grammar::Expression>);

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
    CHECK(reached.size() == result.nodes.size());
    for (const auto* node : result.nodes) CHECK(reached.contains(node));
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
  const auto ll = ttree::generated::Ll::create_parser();
  const auto lr = ttree::generated::Lr::create_parser();
  const auto aux = ttree::generated::Aux::create_parser();
  verify(*ll);
  verify(*lr);
  verify(*aux);
}

}  // namespace cyclic_adopt_cases

namespace nested_run_cases {

namespace {

using Pair = std::array<std::unique_ptr<cursiv::Parser>, 2>;
namespace ast = tnestedrun::ast;

const ast::Groups* read(const cursiv::ParseResult& r) {
  if (!r.accepted || !r.diagnostics.empty() ||
      r.error != cursiv::AstBuildError::none || r.value == nullptr ||
      r.value->type != &cursiv::ast_type_tag<ast::Groups>)
    return nullptr;
  return static_cast<const ast::Groups*>(r.value);
}

std::vector<std::size_t> sizes(const ast::Groups& groups) {
  std::vector<std::size_t> out;
  for (const ast::Group* g : groups.groups) out.push_back(g->xs.size());
  return out;
}

using Sizes = std::vector<std::size_t>;

void groups(const Pair& pair, std::string_view prefix) {
  const std::string emptySource(prefix);
  const std::string runSource = std::string(prefix) + "x x x";
  const std::string cutSource = std::string(prefix) + "x x ; x";
  const std::string eachSource = std::string(prefix) + "x ; x ; x x";
  for (const auto& parser : pair) {
    const auto empty = parser->parse(emptySource);
    const auto* none = read(empty);
    CHECK(none != nullptr && none->groups.empty());

    const auto run = parser->parse(runSource);
    const auto* one = read(run);
    CHECK(one != nullptr && sizes(*one) == Sizes{3});

    const auto cut = parser->parse(cutSource);
    const auto* two = read(cut);
    CHECK(two != nullptr && sizes(*two) == Sizes{2, 1});

    const auto each = parser->parse(eachSource);
    const auto* three = read(each);
    CHECK(three != nullptr && sizes(*three) == Sizes{1, 1, 2});
  }
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  cursiv::test::count_expectations();

  const Pair plain{g::Ll::create_parser(), g::Lr::create_parser()};
  for (const auto& parser : plain) {
    const auto empty = parser->parse("run:");
    const auto* none = read(empty);
    CHECK(none != nullptr && none->items.empty());
    const auto run = parser->parse("run:x x x x");
    const auto* items = read(run);
    CHECK(items != nullptr && items->items.size() == 4);
  }

  groups({g::Ll::create_parser(), g::Lr::create_parser()}, "run:inplace:");
  groups({g::Ll::create_parser(), g::Lr::create_parser()}, "run:behind:");
}

}  // namespace nested_run_cases

namespace ast_materialise_cases { void run(); }

namespace nest_adopt_cases { void run(); }

namespace directive_sequence_cases { void run(); }

namespace data_parts_cases { void run(); }

namespace flag_operand_cases { void run(); }

int main() {
  cursiv::test::count_expectations();
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
