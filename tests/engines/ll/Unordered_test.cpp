#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/UnorderedGrammar.hpp"
#include "engines/ll/UnorderedOptionalGrammar.hpp"
#include "engines/ll/UnorderedTurnsGrammar.hpp"
#include "unorderedll/generated/Ll/Module.hpp"
#include "unorderedlr/generated/Lr/Module.hpp"
#include "unorderedoptll/generated/Ll/Module.hpp"
#include "unorderedoptlr/generated/Lr/Module.hpp"
#include "unorderedturnsll/generated/Ll/Module.hpp"
#include "unorderedturnslr/generated/Lr/Module.hpp"

namespace {

void valid(const cursiv::Parser& parser, std::string_view text,
           std::string_view values = {}) {
  const auto result = parser.parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  const auto* node = result.value_as<tunordered::Record>();
  CHECK(node != nullptr);
  if (!node) return;
  std::string seen;
  for (const auto value : node->values) seen += value;
  CHECK(seen == values);
  if (text.starts_with("pair"))
    CHECK(node->first == "a" && node->second == "b");
  if (text.starts_with("fields"))
    CHECK(node->first == "12" && node->second == "34" && node->flag);
  if (text.starts_with("twin"))
    CHECK(node->first == "12" && node->second == "34");
  if (text.starts_with("blocks")) CHECK(node->flag);
  if (text.starts_with("child")) {
    CHECK(node->flag && node->child != nullptr);
    if (node->child) {
      CHECK(node->child->name == "12" && node->child->parent == node);
      CHECK(node->child->cstNodeId < result.cst().size());
    }
  }
  if (text.starts_with("wrap")) {
    const auto* outer = cursiv::ast_cast<tunordered::Wrap>(node->expression);
    CHECK(outer != nullptr);
    if (!outer) return;
    const auto* inner = cursiv::ast_cast<tunordered::Wrap>(outer->inner);
    CHECK(inner != nullptr && outer->parent == node);
    if (!inner) return;
    const auto* atom = cursiv::ast_cast<tunordered::Name>(inner->inner);
    CHECK(atom != nullptr && inner->parent == outer);
    if (atom) CHECK(atom->name == "12" && atom->parent == inner);
    const bool bangFirst = text.find('!') < text.find('?');
    CHECK(outer->op == (bangFirst ? "?" : "!"));
    CHECK(inner->op == (bangFirst ? "!" : "?"));
  }
  if (text.starts_with("headblock")) {
    const auto* wrap = cursiv::ast_cast<tunordered::Wrap>(node->expression);
    CHECK(wrap != nullptr);
    if (!wrap) return;
    const auto* atom = cursiv::ast_cast<tunordered::Name>(wrap->inner);
    CHECK(wrap->op == "!" && wrap->parent == node && atom != nullptr);
    if (atom) CHECK(atom->name == "12" && atom->parent == wrap);
  }
}

bool offers(const cursiv::Parser& parser, std::string_view prefix,
            std::string_view word) {
  const auto partial = parser.parse(prefix);
  std::vector<cursiv::NextFeature> features;
  parser.nextFeaturesAt(partial.cst(), prefix.size(), prefix.size(), features);
  return std::any_of(features.begin(), features.end(), [&](const auto& f) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    return std::ranges::find(spellings, word) != spellings.end();
  });
}

void exercise(const cursiv::Parser& parser) {
  for (const auto text :
       {"pair a b;",           "pair b a;",      "pair b /*comment*/ a;",
        "nested a b c;",       "nested b a c;",  "nested c a b;",
        "nested c b a;",       "group a b c;",   "group b a c;",
        "group c a b;",        "group c b a;",   "prefix x a x b;",
        "prefix x b x a;",     "duplicate a a;", "child node 12 flag;",
        "child flag node 12;", "twin 12 34 z;",  "twin 12 z 34;",
        "twin z 12 34;",       "optional;",      "optional a b;",
        "optional b a;",       "tight [ab];",    "tight [ba];",
        "wrap 12!?;",          "wrap 12?!;",     "wrapwords 12(!)(?);",
        "wrapwords 12(?)(!);", "edges ab ;",     "edges ba ;",
        "headblock 12!x;",     "headblock x12!;"})
    valid(parser, text);

  std::vector<std::string> fields{"flag", "name 12", "value 34"};
  do {
    const std::string text =
        "fields " + fields[0] + " " + fields[1] + " " + fields[2] + ";";
    valid(parser, text);
  } while (std::next_permutation(fields.begin(), fields.end()));

  // Exhaust every order once. An eight-item copy only repeats these runtime
  // paths while expanding the generated LR automaton exponentially; the
  // Eleven/SixNests lowering checks below cover the supported width limits.
  std::string order = "abcd";
  do {
    std::string text = "four ";
    for (const char c : order) text += std::string(1, c) + " ";
    text += ";";
    valid(parser, text, order);
  } while (std::next_permutation(order.begin(), order.end()));

  for (const auto text :
       {"blocks a a b;", "blocks b a a;", "blocks a /*comment*/ a b;"})
    valid(parser, text, "aa");
  for (const auto text : {"bounded a a b;", "bounded b a a;"})
    valid(parser, text, "aa");
  for (const auto& [text, values] :
       {std::pair{"gather a b;", "ab"}, {"gather b a;", "ba"},
        {"gather a a b b;", "aabb"}, {"gather b b a a;", "bbaa"},
        {"gather b /*comment*/ a a;", "baa"}})
    valid(parser, text, values);

  for (const auto text :
       {"gather;", "gather a a;", "gather b b;", "gather a b a;",
        "gather b a b;", "gather a a b a;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }
  for (const auto text :
       {"pair a;",      "pair b;",        "pair a a;",        "pair a b a;",
        "four a b c;",  "four a a c d;",  "blocks b;",        "blocks a b a;",
        "bounded a b;", "bounded a b a;", "bounded a a a b;", "nested a c b;",
        "prefix x a;",  "group a c b;",   "group b c a;",     "group a b;",
        "group c;",     "duplicate a;",   "optional a;",      "tight [a b];",
        "edges a b ;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }

  CHECK(offers(parser, "fields flag ", "name") &&
        offers(parser, "fields flag ", "value") &&
        !offers(parser, "fields flag ", "flag"));
  CHECK(offers(parser, "gather a ", "a") && offers(parser, "gather a ", "b"));
  CHECK(offers(parser, "gather a b ", "b") &&
        !offers(parser, "gather a b ", "a"));
}

const tunordered::Wrap* wrap_of(const cursiv::ParseResult& result) {
  const auto* node = result.value_as<tunordered::Record>();
  return node ? cursiv::ast_cast<tunordered::Wrap>(node->expression) : nullptr;
}

// The LR lowering's own paths, which both engines read alike: three positions
// read from a region's edges, every order of three nests, and a nest writing
// over its group as over a run.
void lowering_paths(const cursiv::Parser& parser) {
  std::string order = "abc";
  do {
    valid(parser, "trio " + order + ";");
    valid(parser, "trio  " + order + " ;");
  } while (std::next_permutation(order.begin(), order.end()));
  for (const auto text :
       {"trio a bc;", "trio ab;", "trio abca;", "trio aab;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }

  std::string ops = "!?~";
  do {
    const std::string text = "nests 12" + ops + ";";
    valid(parser, text);
    const auto result = parser.parse(text);
    const cursiv::AstNode* at = wrap_of(result);
    for (std::size_t k = ops.size(); k-- > 0;) {
      const auto* wrap = cursiv::ast_cast<tunordered::Wrap>(at);
      CHECK(wrap != nullptr);
      if (!wrap) return;
      CHECK(wrap->op == ops.substr(k, 1));
      at = wrap->inner;
      CHECK(at != nullptr && at->parent == wrap);
    }
    const auto* atom = cursiv::ast_cast<tunordered::Name>(at);
    CHECK(atom != nullptr && atom->name == "12");
  } while (std::next_permutation(ops.begin(), ops.end()));

  for (const auto text : {"nests 12!?;", "nests 12!!?~;", "nests 12~?;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }

  for (const auto text :
       {"flaggroup 12 34!;", "flaggroup 12!34;", "flagrun 12 34!;"}) {
    valid(parser, text);
    const auto result = parser.parse(text);
    const auto* wrap = wrap_of(result);
    CHECK(wrap != nullptr);
    if (!wrap) continue;
    CHECK(wrap->flag && wrap->op.empty() && wrap->child != nullptr);
    if (wrap->child)
      CHECK(wrap->child->name == "34" && wrap->child->parent == wrap);
    const auto* atom = cursiv::ast_cast<tunordered::Name>(wrap->inner);
    CHECK(atom != nullptr && atom->parent == wrap);
    if (atom) CHECK(atom->name == "12");
  }
}

template <class Lowered, class Name>
void refused_alike(const Lowered& t, std::uint16_t rules, Name&& name) {
  CHECK(t.cfg.lowerWhy == cursiv::LowerFail::unorderedTooWide);
  CHECK(t.cfg.lowerRule < rules);
  if (t.cfg.lowerRule >= rules) return;
  const std::string said =
      cursiv::lower_fail_sentence(t.cfg, name(t.cfg, t.cfg.lowerRule));
  CHECK(said.starts_with("rule `") &&
        said.find("Root` holds an unordered group") != std::string::npos);
}

// Both engines refuse a group in the same words.
template <class G>
void refused_too_wide() {
  const auto& lr = cursiv::LrLowering<G>::table();
  refused_alike(lr, lr.cfg.nnts,
                [](const auto& c, std::uint16_t r) { return c.nt_name(r); });
  const auto& ll = cursiv::LlLowering<G>::table();
  refused_alike(ll, ll.cfg.nrules,
                [](const auto& c, std::uint16_t r) { return c.rule_name(r); });
}

// ...and spell the widest one alike.
template <class G>
void spelt() {
  CHECK(!cursiv::LrLowering<G>::table().cfg.lowerFail());
  CHECK(!cursiv::LlLowering<G>::table().cfg.lowerFail());
}

// ...and refuse an item that may read nothing in the same words, naming it.
template <class G>
void refused_empty(std::string_view part) {
  const auto& lr = cursiv::LrLowering<G>::table();
  const auto& ll = cursiv::LlLowering<G>::table();
  CHECK(lr.fault.code == cursiv::GrammarFaultCode::nullableUnorderedItem);
  CHECK(lr.fault == ll.fault && lr.fault.part == part);
  CHECK(lr.cfg.nt_name(lr.fault.rule) == "Root" &&
        ll.cfg.rule_name(ll.fault.rule) == "Root");
  CHECK(cursiv::grammar_fault_text(lr.fault).starts_with(
      "holds an unordered item that may match nothing, `" + std::string(part) +
      "`: an unordered item must read input"));
}

// ...and refuse an optional item in a group that holds a nest, naming it.
template <class G>
void refused_nest(std::string_view part) {
  const auto& lr = cursiv::LrLowering<G>::table();
  const auto& ll = cursiv::LlLowering<G>::table();
  CHECK(lr.fault.code == cursiv::GrammarFaultCode::nullableUnorderedItem);
  CHECK(lr.fault == ll.fault && lr.fault.part == part && lr.fault.gated);
  CHECK(cursiv::grammar_fault_text(lr.fault).starts_with(
      "holds an optional unordered item, `" + std::string(part) +
      "`, in a group whose items hold a `nest`"));
}

// Both engines read one text alike: the verdict, the tree and, where
// `records`, the records under it.
void alike(const cursiv::Parser& ll, const cursiv::Parser& lr,
           const cursiv::AstReflection& llAst,
           const cursiv::AstReflection& lrAst, std::string_view text,
           bool records) {
  const auto a = ll.parse(text), b = lr.parse(text);
  CHECK(a.accepted == b.accepted);
  CHECK(a.diagnostics.size() == b.diagnostics.size());
  std::string x, y;
  if (a.value) cursiv::test::ast_of(llAst, *a.value, x);
  if (b.value) cursiv::test::ast_of(lrAst, *b.value, y);
  if (x != y)
    std::fprintf(stderr, "%.*s\n  LL %s\n  LR %s\n",
                 static_cast<int>(text.size()), text.data(), x.c_str(),
                 y.c_str());
  CHECK(x == y);
  if (records)
    CHECK(cursiv::test::records(a.cst()) == cursiv::test::records(b.cst()));
}

void records_alike() {
  namespace g = tunordered::generated;
  const auto ll = g::Ll::create_parser();
  const auto lr = g::Lr::create_parser();
  const auto llAst = g::Ll::create_ast_reflection();
  const auto lrAst = g::Lr::create_ast_reflection();
  for (const auto text :
       {"pair b /*comment*/ a;", "nested c b a;", "prefix x b x a;",
        "group c b a;", "group a c b;", "child flag node 12;",
        "optional b a;", "tight [ba];",
        "wrapwords 12(?)(!);", "edges ba ;", "fields value 34 flag name 12;",
        "four d b c a ;", "blocks b a a;", "bounded b a a;",
        "pair a a;", "nested a c b;", "gather b b a a;", "gather a b a;"})
    alike(*ll, *lr, *llAst, *lrAst, text, true);
}

// A loop inside an operand is decided in the operand's own rule: `+1` after
// `name` is a label, as it is outside any unordered group.
void loop_in_operand() {
  namespace g = tunorderedturns::generated;
  const auto ll = g::Ll::create_parser();
  const auto lr = g::Lr::create_parser();
  const auto llAst = g::Ll::create_ast_reflection();
  const auto lrAst = g::Lr::create_ast_reflection();
  for (const auto text :
       {"!name +1 x;", "?name +1 x;", "?;name +1 x", "!name +1 +2 x y;",
        "?name +1 +2 x y;", "?;name +1 +2 x y", "?name x;", "?name + 1 x;"})
    alike(*ll, *lr, *llAst, *lrAst, text, true);
  for (const auto text : {"?name +1 x;", "?;name +1 x"}) {
    const auto result = ll->parse(text);
    const auto* doc = result.value_as<tunorderedturns::Doc>();
    CHECK(doc != nullptr);
    if (doc)
      CHECK(doc->tails.empty() && doc->labels.size() == 2 &&
            doc->labels[0] == "+1" && doc->labels[1] == "x");
  }
}

// The tree a text builds, spelled.
std::string tree_of(const cursiv::Parser& parser,
                    const cursiv::AstReflection& ast, std::string_view text) {
  const auto result = parser.parse(text);
  std::string out;
  if (result.value) cursiv::test::ast_of(ast, *result.value, out);
  return out;
}

const toptional::Record* accepted(const cursiv::ParseResult& result,
                                  std::string_view text) {
  const bool clean = result.accepted && result.diagnostics.empty();
  if (!clean)
    std::fprintf(stderr, "not accepted: %.*s\n", static_cast<int>(text.size()),
                 text.data());
  CHECK(clean);
  const auto* node = result.value_as<toptional::Record>();
  CHECK(node != nullptr);
  return clean ? node : nullptr;
}

std::string values_of(const toptional::Record& node) {
  std::string seen;
  for (const auto value : node.values) seen += value;
  return seen;
}

void refused(const cursiv::Parser& parser, std::string_view text) {
  const auto result = parser.parse(text);
  if (result.accepted && result.diagnostics.empty())
    std::fprintf(stderr, "accepted: %.*s\n", static_cast<int>(text.size()),
                 text.data());
  CHECK(!result.accepted || !result.diagnostics.empty());
}

// Optional items: every order of the items present, with any of the optional
// ones absent. An absent item writes nothing, so the tree is the one the same
// items build in order around `option`.
void optional_items(const cursiv::Parser& parser,
                    const cursiv::AstReflection& ast) {
  const std::vector<std::string> optional{"b 2", "c", "d d"};
  for (unsigned present = 0; present < 8; ++present) {
    std::vector<std::string> items{"a 1"};
    std::string ordered = "ordered a 1";
    for (unsigned i = 0; i < 3; ++i)
      if ((present >> i & 1u) != 0) {
        items.push_back(optional[i]);
        ordered += " " + optional[i];
      }
    ordered += ";";
    const std::string tree = tree_of(parser, ast, ordered);
    std::sort(items.begin(), items.end());
    do {
      std::string text = "mix";
      for (const auto& item : items) text += " " + item;
      text += ";";
      const auto result = parser.parse(text);
      const auto* node = accepted(result, text);
      if (!node) continue;
      CHECK(node->first == "1");
      CHECK(node->second == ((present & 1u) != 0 ? "2" : ""));
      CHECK(node->flag == ((present & 2u) != 0));
      CHECK(values_of(*node) == ((present & 4u) != 0 ? "dd" : ""));
      CHECK(tree_of(parser, ast, text) == tree);
    } while (std::next_permutation(items.begin(), items.end()));
  }
  // An item reads once, a repeated one as one run, and a required one always.
  for (const auto text :
       {"mix a 1 a 2;", "mix a 1 b 2 b 3;", "mix c a 1 c;", "mix d a 1 d;",
        "mix b 2 c d;", "mix c;", "mix;"})
    refused(parser, text);

  // A group of optional items only may match nothing.
  for (const auto& [text, first, second] :
       {std::tuple{"opt;", "", ""}, {"opt a;", "a", ""}, {"opt b;", "", "b"},
        {"opt a b;", "a", "b"}, {"opt b a;", "a", "b"}}) {
    const auto result = parser.parse(text);
    const auto* node = accepted(result, text);
    if (node) CHECK(node->first == first && node->second == second);
  }
  for (const auto text : {"opt a a;", "opt b b;", "opt a b a;"})
    refused(parser, text);

  for (const auto& [text, values] :
       {std::pair{"count b;", ""}, {"count a b;", "a"}, {"count b a;", "a"},
        {"count a a b;", "aa"}, {"count b a a;", "aa"}}) {
    const auto result = parser.parse(text);
    const auto* node = accepted(result, text);
    if (node) CHECK(values_of(*node) == values);
  }
  for (const auto text :
       {"count a a a b;", "count a b a;", "count a;", "count;"})
    refused(parser, text);

  for (const auto& [text, first] :
       {std::pair{"region <> b;", "<>"}, {"region b <>;", "<>"},
        {"region b;", ""}}) {
    const auto result = parser.parse(text);
    const auto* node = accepted(result, text);
    if (node) CHECK(node->first == first);
  }
  for (const auto text : {"region < > b;", "region <>;", "region b <> <>;"})
    refused(parser, text);

  // The node made over the group stands with every item absent.
  for (const auto& [text, name] :
       {std::pair{"child;", ""}, {"child x;", ""}, {"child 12;", "12"},
        {"child x 12;", "12"}, {"child 12 x;", "12"}}) {
    const auto result = parser.parse(text);
    const auto* node = accepted(result, text);
    if (!node) continue;
    CHECK(node->child != nullptr);
    if (node->child)
      CHECK(node->child->name == name && node->child->parent == node);
  }
  for (const auto text : {"child x x;", "child 12 34;"}) refused(parser, text);

  // Inside the group, the items still unread are offered, and what follows
  // the group once only optional ones are left.
  CHECK(offers(parser, "mix a 1 ", "b") && offers(parser, "mix a 1 ", "c") &&
        offers(parser, "mix a 1 ", "d") && offers(parser, "mix a 1 ", ";") &&
        !offers(parser, "mix a 1 ", "a"));
  CHECK(offers(parser, "mix b 2 ", "a") && offers(parser, "mix b 2 ", "c") &&
        offers(parser, "mix b 2 ", "d") && !offers(parser, "mix b 2 ", "b") &&
        !offers(parser, "mix b 2 ", ";"));
  CHECK(offers(parser, "mix a 1 d ", "d") && offers(parser, "mix a 1 d ", ";") &&
        offers(parser, "mix a 1 d ", "b"));
  CHECK(offers(parser, "opt ", "a") && offers(parser, "opt ", "b") &&
        offers(parser, "opt ", ";"));
  CHECK(offers(parser, "opt b ", "a") && offers(parser, "opt b ", ";") &&
        !offers(parser, "opt b ", "b"));

  // A required item missing is repaired, not a crash.
  for (const auto text : {"mix b 2;", "mix c d b 2;", "count a;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.diagnostics.empty());
  }
}

void optional_alike() {
  namespace g = toptional::generated;
  const auto ll = g::Ll::create_parser();
  const auto lr = g::Lr::create_parser();
  const auto llAst = g::Ll::create_ast_reflection();
  const auto lrAst = g::Lr::create_ast_reflection();
  optional_items(*ll, *llAst);
  optional_items(*lr, *lrAst);
  for (const auto text :
       {"mix d c a 1 b 2;", "mix a 1;", "mix c a 1;", "ordered a 1 d;",
        "opt;", "opt b a;", "count b a a;", "region b;", "region b <>;",
        "child;", "child x 12;", "mix b 2;", "opt a a;"})
    alike(*ll, *lr, *llAst, *lrAst, text, true);
}

}  // namespace

int main() {
  records_alike();
  loop_in_operand();
  const auto ll = tunordered::generated::Ll::create_parser();
  const auto lr = tunordered::generated::Lr::create_parser();
  exercise(*ll);
  exercise(*lr);
  lowering_paths(*ll);
  lowering_paths(*lr);
  spelt<tunordered::Eleven>();
  spelt<tunordered::SixNests>();
  spelt<tunordered::ElevenOptional>();
  optional_alike();
  refused_empty<toptional::NullableItem>("\"a\"? \"b\"?");
  refused_empty<toptional::OptionalOption>("(\"a\"?)?");
  refused_nest<toptional::OptionalNest>("({Wrap.inner=} (op='?'))?");
  refused_too_wide<tunordered::Twelve>();
  refused_too_wide<tunordered::SevenNests>();
  return cursiv::test::report("Unordered_test");
}
