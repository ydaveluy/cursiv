#include "asttreell/generated/Ll/Module.hpp"
#include "asttreelr/generated/Lr/Module.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/CursivLexicon.hpp"
#include "cursiv/FuzzLane.hpp"
#include "cursiv/AstText.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/AstMaterialiseGrammars.hpp"
#include "engines/shared/AstTreeGrammars.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ast_materialise_cases {

namespace {

struct Reader {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
  std::string_view prefix;
};

using Pair = std::array<Reader, 2>;

Pair readers(std::string_view prefix) {
  namespace g = ttree::generated;
  return {Reader{g::Ll::create_parser(), g::Ll::create_ast_reflection(), prefix},
          Reader{g::Lr::create_parser(), g::Lr::create_ast_reflection(), prefix}};
}

std::string tree(const Reader& r, const cursiv::ParseResult& result) {
  std::string out;
  if (result.value) cursiv::test::ast_of(*r.reflection, *result.value, out);
  return out;
}

void expect(const Pair& readers, std::string_view text, std::string_view want) {
  for (const Reader& r : readers) {
    const std::string source = "mat:" + std::string(r.prefix) + std::string(text);
    const auto result = r.parser->parse(source);
    CHECK(result.accepted && result.diagnostics.empty() &&
          !result.cst().recovered());
    CHECK(result.error == cursiv::AstBuildError::none);
    const std::string got = tree(r, result);
    if (got != want)
      std::printf("'%.*s'\n  built %s\n  wants %.*s\n",
                  static_cast<int>(text.size()), text.data(), got.c_str(),
                  static_cast<int>(want.size()), want.data());
    CHECK(got == want);
  }
}

template <class G>
void fuzz(const char* name, const Reader& r) {
  cursiv::fuzzer::lane<G>(name, *r.parser, *r.reflection, 200, 0x5eed);
}

void part_span(const cursiv::ParseResult& result, const cursiv::AstNode* node,
               const cursiv::AstNode* parent, std::size_t begin,
               std::size_t end) {
  const bool valid = node && parent &&
                     node->cstNodeId < result.cst().size() &&
                     parent->cstNodeId < result.cst().size();
  CHECK(valid);
  if (!valid) return;
  const auto& span = result.cst()[node->cstNodeId];
  const auto& owner = result.cst()[parent->cstNodeId];
  if (span.begin != begin || span.end != end)
    std::fprintf(stderr, "materialised span: got %u..%u, expected %zu..%zu\n",
                 span.begin, span.end, begin, end);
  CHECK(span.begin == begin && span.end == end);
  CHECK(result.cst().text(node->cstNodeId) ==
        result.cst().text().substr(begin, end - begin));
  CHECK(node->parent == parent);
  CHECK(owner.begin <= span.begin && span.end <= owner.end);
  CHECK(result.cst().contains(parent->cstNodeId, node->cstNodeId));
}

void successive_assigned_and_appended_values_keep_their_spans(const Reader& e) {
  const std::string source = "mat:payloadcreate:a; b.c; d[2]";
  const auto result = e.parser->parse(source);
  const auto* holder = result.value_as<tmaterialise::Holder>();
  CHECK(result.accepted && !result.cst().recovered() &&
        result.diagnostics.empty() && holder && holder->kids.size() == 2);
  if (!holder || holder->kids.size() != 2) return;
  const auto* prop = cursiv::ast_cast<tmaterialise::Prop>(holder->kids[0]);
  const auto* index = cursiv::ast_cast<tmaterialise::Index>(holder->kids[1]);
  CHECK(prop && index);
  part_span(result, holder->value, holder, 18, 19);
  part_span(result, prop, holder, 21, 24);
  if (prop) part_span(result, prop->object, prop, 21, 22);
  part_span(result, index, holder, 26, 30);
  if (index) {
    part_span(result, index->object, index, 26, 27);
    part_span(result, index->index, index, 28, 29);
  }

  const std::string adoptedSource = "chained:a.b c";
  const auto adopted = e.parser->parse(adoptedSource);
  const auto* block = adopted.value_as<ttree::ast::Block>();
  CHECK(adopted.accepted && !adopted.cst().recovered() &&
        adopted.diagnostics.empty() && block && block->atoms.size() == 2);
  if (!block || block->atoms.size() != 2) return;
  const auto* selected = cursiv::ast_cast<ttree::ast::Index>(block->atoms[0]);
  CHECK(selected);
  part_span(adopted, selected, block, 8, 11);
  if (selected) {
    part_span(adopted, selected->array, selected, 8, 9);
    part_span(adopted, selected->index, selected, 10, 11);
  }
  part_span(adopted, block->atoms[1], block, 12, 13);
}

void stacked_wrappers_and_post_operand_flags_keep_their_meaning(const Reader& e) {
  const std::string source = "mat:stacked:a x";
  const auto result = e.parser->parse(source);
  CHECK(result.accepted && !result.cst().recovered() &&
        result.diagnostics.empty() &&
        result.error == cursiv::AstBuildError::none);
  const auto* root = cursiv::ast_cast<tmaterialise::Box>(result.value);
  CHECK(root && !root->parent && root->cstNodeId < result.cst().size());
  if (!root || root->cstNodeId >= result.cst().size()) return;
  CHECK(result.cst()[root->cstNodeId].begin == 0 &&
        result.cst()[root->cstNodeId].end == source.size());
  CHECK(result.cst().text(root->cstNodeId) == source);

  std::unordered_set<const cursiv::AstNode*> seen;
  const tmaterialise::Flags* current = root;
  const cursiv::AstNode* parent = nullptr;
  for (unsigned depth = 0; depth < 20; ++depth) {
    const auto* box = cursiv::ast_cast<tmaterialise::Box>(current);
    CHECK(box && box->parent == parent && seen.insert(box).second);
    if (!box) return;
    // The flag runs after all twenty wrappers: it lands on the final outer
    // Box, rather than on the node current before its operand.
    CHECK(box->on == (depth == 0));
    if (parent) part_span(result, box, parent, 12, 13);
    parent = box;
    current = box->in;
  }
  CHECK(current && current->type == &cursiv::ast_type_tag<tmaterialise::Flags> &&
        !current->on && seen.insert(current).second);
  if (current) part_span(result, current, parent, 12, 13);
  CHECK(seen.size() == 21 && result.nodes().size() == 21);
  for (const auto* node : result.nodes()) CHECK(seen.contains(node));
}

void recursive_call_arguments_close_before_the_callers_effects(const Reader& e) {
  constexpr std::string_view prefix = "mat:recursivecalls:";
  for (const std::size_t depth : {0u, 1u, 2u, 16u}) {
    std::string source(prefix);
    source += "$a";
    for (std::size_t i = 0; i < depth; ++i) source += ":$a";
    const auto result = e.parser->parse(source);
    CHECK(result.accepted && !result.cst().recovered() &&
          result.diagnostics.empty());
    CHECK(result.error == cursiv::AstBuildError::none);
    CHECK(result.nodes().size() == 2 * depth + 1);
    const auto* current = cursiv::ast_cast<tmaterialise::Expr>(result.value);
    const cursiv::AstNode* parent = nullptr;
    std::unordered_set<const cursiv::AstNode*> seen;
    for (std::size_t i = 0; i < depth; ++i) {
      const auto* call = cursiv::ast_cast<tmaterialise::Call>(current);
      CHECK(call && call->flagged && call->args.size() == 1);
      if (!call || call->args.size() != 1) break;
      CHECK(seen.insert(call).second);
      CHECK(call->parent == parent);
      const auto& span = result.cst()[call->cstNodeId];
      CHECK(span.begin == (i == 0 ? 0 : prefix.size() + 3 * i) &&
            span.end == source.size());
      const auto* callee = cursiv::ast_cast<tmaterialise::Var>(call->callee);
      CHECK(callee && callee->name == "a");
      if (callee) {
        CHECK(seen.insert(callee).second);
        part_span(result, callee, call, prefix.size() + 3 * i,
                  prefix.size() + 3 * i + 2);
      }
      parent = call;
      current = call->args[0];
    }
    const auto* last = cursiv::ast_cast<tmaterialise::Var>(current);
    CHECK(last && last->name == "a" && last->parent == parent);
    if (last) {
      CHECK(seen.insert(last).second);
      CHECK(result.cst()[last->cstNodeId].begin ==
            (depth == 0 ? 0 : prefix.size() + 3 * depth));
      CHECK(result.cst()[last->cstNodeId].end == source.size());
    }
    CHECK(seen.size() == result.nodes().size());
  }
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  cursiv::test::count_expectations();

  const Pair chain = readers("atomchain:");
  expect(chain, "", "Block{}");
  expect(chain, "A", "Block{}");
  expect(chain, "$a", "Block{atoms0=Var{} }");
  expect(chain, "$a.b", "Block{atoms0=Prop{object=Var{} } }");
  expect(chain, "A $a.b[1] B $c",
         "Block{atoms1=Index{object=Prop{object=Var{} } index=Num{} } "
         "atoms3=Var{} }");
  for (const Reader& r : chain) {
    const auto result = r.parser->parse("mat:atomchain:A $a B");
    const auto* block = result.value_as<tmaterialise::Block>();
    CHECK(block != nullptr && block->atoms.size() == 3);
    if (block == nullptr || block->atoms.size() != 3) continue;
    CHECK(std::get<std::string_view>(block->atoms[0]) == "A");
    CHECK(std::get<tmaterialise::Expr*>(block->atoms[1]) != nullptr);
    CHECK(std::get<std::string_view>(block->atoms[2]) == "B");
  }

  const Pair flanked = readers("flanked:");
  expect(flanked, "[1]", "Row{}");
  expect(flanked, "[1, 2]", "Row{}");
  expect(flanked, "[\n1,\n[2, 3]\n]", "Row{items1=Row{} }");
  expect(flanked, "[[1], [[2]]]",
         "Row{items0=Row{} items1=Row{items0=Row{} } }");
  for (const Reader& r : flanked) {
    const auto result = r.parser->parse("mat:flanked:[1, [2], 3]");
    const auto* row = result.value_as<tmaterialise::Row>();
    CHECK(row != nullptr && row->items.size() == 3);
    if (row == nullptr || row->items.size() != 3) continue;
    CHECK(std::get<std::int64_t>(row->items[0]) == 1);
    const auto* inner = std::get<tmaterialise::Row*>(row->items[1]);
    CHECK(inner != nullptr && inner->items.size() == 1);
    CHECK(std::get<std::int64_t>(row->items[2]) == 3);
  }

  const Pair shared = readers("sharedprefix:");
  expect(shared, "$a.b[1]", "Index{object=Prop{object=Var{} } index=Num{} }");
  expect(shared, "7.x", "Prop{object=Num{} }");
  expect(shared, "-$a.b[1]",
         "Neg{operand=Index{object=Prop{object=Var{} } index=Num{} } }");

  const Pair payload = readers("payloadcreate:");
  expect(payload, "a", "Holder{value=Var{} }");
  expect(payload, "a.b", "Holder{value=Prop{object=Var{} } }");
  expect(payload, "a[1].c",
         "Holder{value=Prop{object=Index{object=Var{} index=Num{} } } }");
  expect(payload, "a; b.c; d[2]",
         "Holder{value=Var{} kids0=Prop{object=Var{} } "
         "kids1=Index{object=Var{} index=Num{} } }");
  for (const Reader& r : payload)
    successive_assigned_and_appended_values_keep_their_spans(r);

  const Pair group = readers("flaggroup:");
  expect(group, "", "Holder{}");
  expect(group, "!$a", "Holder{value=Var{} }");
  expect(group, "!$a; $b; $c", "Holder{value=Var{} kids0=Var{} kids1=Var{} }");
  expect(group, "; $b", "Holder{kids0=Var{} }");
  for (const Reader& r : group) {
    const auto flagged = r.parser->parse("mat:flaggroup:!$a");
    const auto* holder = flagged.value_as<tmaterialise::Holder>();
    CHECK(holder != nullptr && holder->flag);
    const auto plain = r.parser->parse("mat:flaggroup:; $b");
    holder = plain.value_as<tmaterialise::Holder>();
    CHECK(holder != nullptr && !holder->flag);
  }

  const Pair ladder = readers("optionalnest:");
  expect(ladder, "$a", "Var{}");
  expect(ladder, "7", "Num{}");
  expect(ladder, "$a cast as t", "Cast{operand=Var{} }");
  expect(ladder, "$a instance of t", "Inst{operand=Var{} }");
  expect(ladder, "$a cast as t instance of u",
         "Inst{operand=Cast{operand=Var{} } }");
  expect(ladder, "1 + 2", "Bin{left=Num{} right=Num{} }");
  expect(ladder, "1 + $a cast as t + 3",
         "Bin{left=Bin{left=Num{} right=Cast{operand=Var{} } } right=Num{} }");
  expect(ladder, "($a cast as t) + 1",
         "Bin{left=Cast{operand=Var{} } right=Num{} }");
  expect(ladder, "((1))", "Num{}");
  expect(ladder, "-($a + 1)", "Neg{operand=Bin{left=Var{} right=Num{} } }");

  const Pair pass = readers("passthrough:");
  expect(pass, "i", "Var{}");
  expect(pass, "1", "Num{}");
  expect(pass, "(i)", "Var{}");
  expect(pass, "((1))", "Num{}");
  expect(pass, "i=1", "Assign{target=Var{} value=Num{} }");
  expect(pass, "i+1", "Bin{left=Var{} right=Num{} }");
  expect(pass, "i<2", "Bin{left=Var{} right=Num{} }");
  expect(pass, "x=a<b",
         "Assign{target=Var{} value=Bin{left=Var{} right=Var{} } }");

  const Pair relay = readers("relay:");
  expect(relay, "!$x", "Neg{operand=Var{} }");
  expect(relay, "!($x+$y)", "Neg{operand=Bin{left=Var{} right=Var{} } }");
  expect(relay, "!!$x", "Neg{operand=Neg{operand=Var{} } }");
  expect(
      relay, "(!$x)+[$a,$b]",
      "Bin{left=Neg{operand=Var{} } right=Pair{first=Var{} second=Var{} } }");
  expect(relay, "[$x,$y]", "Pair{first=Var{} second=Var{} }");

  const Pair terminal = readers("unassignedterminal:");
  expect(terminal, "fn f", "Var{}");
  expect(terminal, "new.target", "Target{}");
  for (const Reader& r : terminal) {
    const auto result = r.parser->parse("mat:unassignedterminal:fn f");
    const auto* var = result.value_as<tmaterialise::Var>();
    CHECK(var != nullptr && var->name == "f");
  }

  // The node of a call a repair gave up is absent, and only the repair says
  // so: the build adds no diagnostic, whether the document or a member was
  // owed that node. Where the repair gives up the call alone, the holder
  // stands without it.
  const Pair givenUp = readers({});
  for (const Reader& r : givenUp)
    for (const char* text : {"mat:givenup:x n a ? ;", "mat:passed:n a ?"}) {
      const auto result = r.parser->parse(text);
      CHECK(result.accepted && !result.diagnostics.empty());
      CHECK(result.value != nullptr ||
            result.error == cursiv::AstBuildError::no_entry);
      for (const cursiv::ParseDiagnostic& d : result.diagnostics)
        CHECK(d.kind != cursiv::DiagnosticKind::conversion_error);
    }
  const auto held = givenUp[0].parser->parse("mat:givenup:x n a ? ;");
  CHECK(tree(givenUp[0], held) == "Holder{}");

  const Reader ll{g::Ll::create_parser(), g::Ll::create_ast_reflection(), {}};
  const Reader lr{g::Lr::create_parser(), g::Lr::create_ast_reflection(), {}};
  stacked_wrappers_and_post_operand_flags_keep_their_meaning(ll);
  stacked_wrappers_and_post_operand_flags_keep_their_meaning(lr);
  recursive_call_arguments_close_before_the_callers_effects(ll);
  recursive_call_arguments_close_before_the_callers_effects(lr);
  fuzz<tmaterialise::FuzzSuite>("AstMaterialiseLl", ll);
  fuzz<tmaterialise::FuzzSuite>("AstMaterialiseLr", lr);
}

}  // namespace ast_materialise_cases

namespace nest_adopt_cases {

// A nest behind a called rule wraps the node that rule made, in both engines:
// the wrapper holds it and no empty node stands in its place.

namespace {

struct Engine {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
  const char* name;
};

std::string tree(const Engine& e, std::string_view text, std::size_t& nodes) {
  const std::string source = "mat:adopt:" + std::string(text);
  const cursiv::ParseResult r = e.parser->parse(source);
  std::string out;
  nodes = 0;
  if (!r.accepted || !r.diagnostics.empty() || r.value == nullptr)
    return "<refused>";
  nodes = r.nodes().size();
  cursiv::test::ast_of(*e.reflection, *r.value, out);
  return out;
}

void check(const Engine& e, std::string_view text, std::string_view expected,
           std::size_t expectedNodes) {
  std::size_t nodes = 0;
  const std::string got = tree(e, text, nodes);
  if (got != expected)
    std::printf("  %.*s\n    got      %s\n    expected %.*s\n",
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
  CHECK(nodes == expectedNodes);
}

void a_nest_wraps_the_node_the_called_rule_made(const Engine& e) {
  check(e, "one a[b]", "Index{array=Ident{} index=Ident{} }", 3);
  check(e, "some a[b]", "Index{array=Ident{} index=Ident{} }", 3);
  check(e, "some a[b][c]",
        "Index{array=Index{array=Ident{} index=Ident{} } index=Ident{} }", 5);
}

void the_wrapped_node_stays_where_a_member_takes_the_rule(const Engine& e) {
  check(e, "assign a = b", "Assign{target=Ident{} value=Ident{} }", 3);
  check(e, "assign a[b] = c",
        "Assign{target=Index{array=Ident{} index=Ident{} } value=Ident{} }", 5);
}

void a_wrapper_of_another_family_wraps_what_its_member_holds(const Engine& e) {
  check(e, "semi a ;", "Stmt{value=Ident{} }", 2);
}

constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

struct PrefixNode {
  char kind;  // N: name, D: number, C: call, S: selector, I: index.
  std::size_t begin, end;
  std::string_view spelling;
  std::size_t parent = kNoParent;
};

struct PrefixCase {
  std::string_view text;
  std::vector<PrefixNode> nodes;  // Preorder, with independently stated spans.
};

void exact_prefixes(const Engine& e, std::string_view mode, const PrefixCase& c,
                    bool compareText = true) {
  namespace ast = tnestadopt::ast;
  const std::string source =
      "mat:adopt:" + std::string(mode) + " " + std::string(c.text);
  const std::size_t begin = source.size() - c.text.size();
  const auto result = e.parser->parse(source);
  bool good = true;
  unsigned reported = 0;
  const auto verify = [&](bool condition, const char* message,
                          std::size_t index = kNoParent) {
    if (condition) return;
    good = false;
    if (reported++ < 6) {
      const auto shown = c.text.substr(0, 80);
      std::fprintf(stderr, "nest prefix [%s] %.*s '%.*s' node=%zu: %s\n",
                   e.name, static_cast<int>(mode.size()), mode.data(),
                   static_cast<int>(shown.size()), shown.data(), index,
                   message);
    }
  };
  verify(result.accepted && !result.cst().recovered() &&
             result.diagnostics.empty() &&
             result.error == cursiv::AstBuildError::none,
         "clean acceptance without AST build errors");
  const auto* statement = result.value_as<ast::Stmt>();
  verify(statement && statement->value && !statement->parent,
         "detached statement owns the expression");
  if (!statement || !statement->value) {
    CHECK(good);
    return;
  }
  verify(result.cst().text() == source, "complete input retained");
  // A copied growing prefix would make CST storage quadratic. The fixture's
  // fixed rule/token overhead fits comfortably under this linear allowance.
  verify(result.cst().size() <= 16 * (source.size() + 1),
         "CST storage stays linear in the fixed fixture input");

  struct Visit {
    const cursiv::AstNode* node;
    std::size_t parent;
  };
  std::vector<Visit> pending{{statement->value, kNoParent}}, visited;
  pending.reserve(c.nodes.size());
  visited.reserve(c.nodes.size());
  std::unordered_set<const cursiv::AstNode*> seen;
  seen.reserve(c.nodes.size() + 1);
  seen.insert(statement);
  std::size_t roots = 0;
  e.reflection->visitMembers(*statement, [&](auto, auto, const auto& child) {
    ++roots;
    verify(&child == statement->value, "statement has exactly its value child");
  });
  verify(roots == 1, "statement has one expression child");
  while (!pending.empty()) {
    const Visit current = pending.back();
    pending.pop_back();
    if (!seen.insert(current.node).second) {
      verify(false, "each allocated node is reached once");
      continue;
    }
    const std::size_t index = visited.size();
    visited.push_back(current);
    const auto* owner = current.parent == kNoParent
                            ? static_cast<const cursiv::AstNode*>(statement)
                            : visited[current.parent].node;
    verify(current.node->parent == owner, "correct containing AST parent",
           index);
    const bool valid = current.node->cstNodeId < result.cst().size() &&
                       owner->cstNodeId < result.cst().size();
    verify(valid, "valid child and owner CST ids", index);
    if (valid) {
      const auto& outer = result.cst()[owner->cstNodeId];
      const auto& inner = result.cst()[current.node->cstNodeId];
      verify(outer.begin <= inner.begin && inner.end <= outer.end,
             "numeric owner/child span containment", index);
      verify(result.cst().contains(owner->cstNodeId, current.node->cstNodeId),
             "structural CST containment", index);
      verify(inner.begin <= inner.end && inner.end <= source.size(),
             "span is inside the live input", index);
    }
    const std::size_t firstChild = pending.size();
    e.reflection->visitMembers(*current.node,
                               [&](auto, auto, const auto& child) {
                                 pending.push_back({&child, index});
                               });
    std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(firstChild),
                 pending.end());
  }
  verify(visited.size() == c.nodes.size(), "exact expected AST shape");
  for (std::size_t i = 0; i < visited.size() && i < c.nodes.size(); ++i) {
    const auto& got = visited[i];
    const auto& want = c.nodes[i];
    verify(got.parent == want.parent, "exact expected tree edge", i);
    const auto* node = got.node;
    bool typed = false;
    switch (want.kind) {
      case 'N': {
        const auto* name = cursiv::ast_cast<ast::Ident>(node);
        typed = name && name->name == want.spelling;
        break;
      }
      case 'D': {
        const auto* number = cursiv::ast_cast<ast::Number>(node);
        typed = number && number->text == want.spelling;
        break;
      }
      case 'C': {
        const auto* call = cursiv::ast_cast<ast::Call>(node);
        typed = call && call->target;
        break;
      }
      case 'S': {
        const auto* selector = cursiv::ast_cast<ast::Selector>(node);
        typed =
            selector && selector->object && selector->field == want.spelling;
        break;
      }
      case 'I': {
        const auto* at = cursiv::ast_cast<ast::Index>(node);
        typed = at && at->array && at->index;
        break;
      }
    }
    verify(typed, "expected concrete AST type and captured spelling", i);
    if (node->cstNodeId >= result.cst().size()) continue;
    const auto& span = result.cst()[node->cstNodeId];
    const bool exact =
        span.begin == begin + want.begin && span.end == begin + want.end;
    if (!exact && reported < 6)
      std::fprintf(stderr, "  got bytes=%u..%u, expected=%zu..%zu\n",
                   span.begin, span.end, begin + want.begin, begin + want.end);
    verify(exact, "exact nonzero-offset expression prefix bounds", i);
    if (compareText)
      verify(result.cst().text(node->cstNodeId) ==
                 std::string_view(source).substr(begin + want.begin,
                                                 want.end - want.begin),
             "exact prefix text", i);
  }
  verify(result.nodes().size() == c.nodes.size() + 1,
         "exact allocation count, including the statement");
  verify(seen.size() == result.nodes().size(),
         "reachable count equals arena count");
  for (const auto* node : result.nodes())
    verify(seen.contains(node), "every arena allocation is reachable");
  CHECK(good);
}

void refuses(const Engine& e, std::string_view mode, std::string_view text) {
  const std::string source =
      "mat:adopt:" + std::string(mode) + " " + std::string(text);
  const auto result = e.parser->parse(source);
  const bool clean = result.accepted && !result.cst().recovered() &&
                     result.diagnostics.empty() && result.value;
  if (clean)
    std::fprintf(stderr, "nest prefix [%s] %s unexpectedly accepted\n", e.name,
                 source.c_str());
  CHECK(!clean);
}

void a_nullable_payload_still_wraps_the_receiver(const Engine& e) {
  exact_prefixes(e, "nullablenest",
                 {"a", {{'C', 0, 1, {}}, {'N', 0, 1, "a", 0}}});
  exact_prefixes(e, "nullablenest",
                 {"a()", {{'C', 0, 3, {}}, {'N', 0, 1, "a", 0}}});
}

void a_receiver_created_before_any_text_has_an_empty_prefix(const Engine& e) {
  exact_prefixes(e, "emptyhead",
                 {"()", {{'C', 0, 2, {}}, {'N', 0, 0, {}, 0}}});
}

void a_guard_preserves_the_empty_receiver_boundary(const Engine& e) {
  exact_prefixes(e, "guardedempty",
                 {"()", {{'C', 0, 2, {}}, {'N', 0, 0, {}, 0}}});
}

void a_nest_inside_another_payload_keeps_both_prefixes(const Engine& e) {
  exact_prefixes(e, "nestedpayload",
                 {"a.b()", {{'C', 0, 5, {}}, {'S', 0, 3, "b", 0},
                            {'N', 0, 1, "a", 1}}});
}

void optional_calls_and_selector_runs_keep_every_prefix(const Engine& e) {
  const PrefixCase cases[] = {
      {"a", {{'N', 0, 1, "a"}}},
      {"a()", {{'C', 0, 3, {}}, {'N', 0, 1, "a", 0}}},
      {"a.b", {{'S', 0, 3, "b"}, {'N', 0, 1, "a", 0}}},
      {"a.b()", {{'C', 0, 5, {}}, {'S', 0, 3, "b", 0}, {'N', 0, 1, "a", 1}}},
      {"a().b", {{'S', 0, 5, "b"}, {'C', 0, 3, {}, 0}, {'N', 0, 1, "a", 1}}},
      {"a().b()",
       {{'C', 0, 7, {}},
        {'S', 0, 5, "b", 0},
        {'C', 0, 3, {}, 1},
        {'N', 0, 1, "a", 2}}},
      {"a().b().c",
       {{'S', 0, 9, "c"},
        {'C', 0, 7, {}, 0},
        {'S', 0, 5, "b", 1},
        {'C', 0, 3, {}, 2},
        {'N', 0, 1, "a", 3}}},
      {"a.b().c",
       {{'S', 0, 7, "c"},
        {'C', 0, 5, {}, 0},
        {'S', 0, 3, "b", 1},
        {'N', 0, 1, "a", 2}}},
      {"a( ) . b()",
       {{'C', 0, 10, {}},
        {'S', 0, 8, "b", 0},
        {'C', 0, 4, {}, 1},
        {'N', 0, 1, "a", 2}}},
  };
  for (const std::string_view mode :
       {"shortadopt", "shortcreate", "shortleft", "shortcycle"}) {
    for (const auto& c : cases) exact_prefixes(e, mode, c);
    for (const std::string_view text : {"a.", "a(", "a.b(", "a()()"})
      refuses(e, mode, text);
  }
}

void guarded_calls_and_indices_keep_every_prefix(const Engine& e) {
  const PrefixCase indices[] = {
      {"data", {{'N', 0, 4, "data"}}},
      {"data[0]",
       {{'I', 0, 7, {}}, {'N', 0, 4, "data", 0}, {'D', 5, 6, "0", 0}}},
      {"data[ 0 ]",
       {{'I', 0, 9, {}}, {'N', 0, 4, "data", 0}, {'D', 6, 7, "0", 0}}},
      {"data[0][ 1 ]",
       {{'I', 0, 12, {}},
        {'I', 0, 7, {}, 0},
        {'N', 0, 4, "data", 1},
        {'D', 5, 6, "0", 1},
        {'D', 9, 10, "1", 0}}},
  };
  const PrefixCase calls[] = {
      {"data(0)",
       {{'C', 0, 7, {}}, {'N', 0, 4, "data", 0}, {'D', 5, 6, "0", 0}}},
      {"data(0)[ 1 ]",
       {{'I', 0, 12, {}},
        {'C', 0, 7, {}, 0},
        {'N', 0, 4, "data", 1},
        {'D', 5, 6, "0", 1},
        {'D', 9, 10, "1", 0}}},
      {"data[0](1)",
       {{'C', 0, 10, {}},
        {'I', 0, 7, {}, 0},
        {'N', 0, 4, "data", 1},
        {'D', 5, 6, "0", 1},
        {'D', 8, 9, "1", 0}}},
      {"data()[1]",
       {{'I', 0, 9, {}},
        {'C', 0, 6, {}, 0},
        {'N', 0, 4, "data", 1},
        {'D', 7, 8, "1", 0}}},
      {"data(a)[b]",
       {{'I', 0, 10, {}},
        {'C', 0, 7, {}, 0},
        {'N', 0, 4, "data", 1},
        {'N', 5, 6, "a", 1},
        {'N', 8, 9, "b", 0}}},
      {"data( 0 )[1]",
       {{'I', 0, 12, {}},
        {'C', 0, 9, {}, 0},
        {'N', 0, 4, "data", 1},
        {'D', 6, 7, "0", 1},
        {'D', 10, 11, "1", 0}}},
  };
  const PrefixCase spacedIndex{
      "data [0]",
      {{'I', 0, 8, {}}, {'N', 0, 4, "data", 0}, {'D', 6, 7, "0", 0}}};
  const PrefixCase spacedTail{"data(0) [1]",
                              {{'I', 0, 11, {}},
                               {'C', 0, 7, {}, 0},
                               {'N', 0, 4, "data", 1},
                               {'D', 5, 6, "0", 1},
                               {'D', 9, 10, "1", 0}}};
  for (const std::string_view mode :
       {"indexdirect", "tailchoice", "tailouter", "tailinner"}) {
    for (const auto& c : indices) exact_prefixes(e, mode, c);
    const bool guarded = mode == "tailouter" || mode == "tailinner";
    if (guarded)
      refuses(e, mode, spacedIndex.text);
    else
      exact_prefixes(e, mode, spacedIndex);
    if (mode != "indexdirect") {
      for (const auto& c : calls) exact_prefixes(e, mode, c);
      if (guarded)
        refuses(e, mode, spacedTail.text);
      else
        exact_prefixes(e, mode, spacedTail);
    }
    for (const std::string_view text : {"data[", "data[0)", "data(0]"})
      refuses(e, mode, text);
  }
}

void deep_chains_keep_linear_tree_storage(const Engine& e) {
  // 257 wrappers exercise growing prefix state without giant fixtures.
  constexpr std::size_t pairs = 128;
  std::string text = "a";
  text.reserve(4 * pairs + 3);
  std::vector<PrefixNode> forward{{'N', 0, 1, "a"}}, expected;
  forward.reserve(2 * pairs + 2);
  text += "()";
  forward.push_back({'C', 0, text.size(), {}});
  for (std::size_t i = 0; i < pairs; ++i) {
    text += ".b";
    forward.push_back({'S', 0, text.size(), "b"});
    text += "()";
    forward.push_back({'C', 0, text.size(), {}});
  }
  expected.reserve(forward.size());
  for (std::size_t i = forward.size(); i > 0; --i) {
    auto node = forward[i - 1];
    node.parent = expected.empty() ? kNoParent : expected.size() - 1;
    expected.push_back(node);
  }
  const PrefixCase shortChain{text, std::move(expected)};
  for (const std::string_view mode :
       {"shortadopt", "shortcreate", "shortleft", "shortcycle"})
    exact_prefixes(e, mode, shortChain, false);

  constexpr std::size_t depth = 257;
  std::string guardedText = "data";
  guardedText.reserve(4 + 3 * depth);
  expected.clear();
  expected.reserve(2 * depth + 1);
  for (std::size_t i = 0; i < depth; ++i)
    guardedText += i % 2 == 0 ? "(0)" : "[1]";
  for (std::size_t i = depth; i > 0; --i)
    expected.push_back({(i - 1) % 2 == 0 ? 'C' : 'I',
                        0,
                        4 + 3 * i,
                        {},
                        i == depth ? kNoParent : depth - i - 1});
  expected.push_back({'N', 0, 4, "data", depth - 1});
  for (std::size_t i = 0; i < depth; ++i)
    expected.push_back(
        {'D', 5 + 3 * i, 6 + 3 * i, i % 2 == 0 ? "0" : "1", depth - i - 1});
  const PrefixCase guardedChain{guardedText, std::move(expected)};
  exact_prefixes(e, "tailouter", guardedChain, false);
}

void every_engine(const Engine& e) {
  a_nest_wraps_the_node_the_called_rule_made(e);
  the_wrapped_node_stays_where_a_member_takes_the_rule(e);
  a_wrapper_of_another_family_wraps_what_its_member_holds(e);
  a_nullable_payload_still_wraps_the_receiver(e);
  a_receiver_created_before_any_text_has_an_empty_prefix(e);
  a_guard_preserves_the_empty_receiver_boundary(e);
  a_nest_inside_another_payload_keeps_both_prefixes(e);
  optional_calls_and_selector_runs_keep_every_prefix(e);
  guarded_calls_and_indices_keep_every_prefix(e);
  deep_chains_keep_linear_tree_storage(e);
}

}  // namespace

void run() {
  every_engine({ttree::generated::Ll::create_parser(),
                ttree::generated::Ll::create_ast_reflection(), "LL"});
  every_engine({ttree::generated::Lr::create_parser(),
                ttree::generated::Lr::create_ast_reflection(), "LR"});
}

}  // namespace nest_adopt_cases
