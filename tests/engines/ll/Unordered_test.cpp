#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/UnorderedGrammar.hpp"
#include "engines/ll/UnorderedTurnsGrammar.hpp"
#include "unorderedendsll/generated/EndsLl/Module.hpp"
#include "unorderedll/generated/Ll/Module.hpp"
#include "unorderedlr/generated/Lr/Module.hpp"
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
  if (text.starts_with("modifiers")) {
    CHECK(node->first == "12");
    CHECK(node->flag == (text.find("static") != std::string_view::npos));
    CHECK(node->sealed == (text.find("final") != std::string_view::npos));
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
    return parser.getGrammar().terminalText(f.feature) == word;
  });
}

void exercise(const cursiv::Parser& parser) {
  for (const auto text :
       {"pair a b;",      "pair b a;",           "pair b /*comment*/ a;",
        "nested a b c;",  "nested b a c;",       "nested c a b;",
        "nested c b a;",  "prefix x a x b;",     "prefix x b x a;",
        "duplicate a a;", "child node 12 flag;", "child flag node 12;",
        "twin 12 34 z;",  "twin 12 z 34;",       "twin z 12 34;",
        "optional;",      "optional a b;",       "optional b a;",
        "tight [ab];",    "tight [ba];",         "wrap 12!?;",
        "wrap 12?!;",     "wrapwords 12(!)(?);", "wrapwords 12(?)(!);",
        "edges ab ;",     "edges ba ;",          "headblock 12!x;",
        "headblock x12!;"})
    valid(parser, text);

  std::vector<std::string> fields{"flag", "name 12", "value 34"};
  do {
    const std::string text =
        "fields " + fields[0] + " " + fields[1] + " " + fields[2] + ";";
    valid(parser, text);
  } while (std::next_permutation(fields.begin(), fields.end()));

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
       {std::pair{"maybe;", ""}, {"maybe a;", "a"}, {"maybe b;", "b"},
        {"maybe a b;", "ab"}, {"maybe b a;", "ba"},
        {"maybe b /*comment*/ a;", "ba"}, {"gather;", ""},
        {"gather a a;", "aa"}, {"gather b b;", "bb"},
        {"gather a a b b;", "aabb"}, {"gather b b a a;", "bbaa"},
        {"gather b a a;", "baa"}})
    valid(parser, text, values);
  for (const auto text :
       {"modifiers 12;", "modifiers static 12;", "modifiers 12 final;",
        "modifiers final static 12;", "modifiers static 12 final;",
        "modifiers 12 static final;", "modifiers final 12 static;"})
    valid(parser, text);
  for (const auto& [text, name] :
       {std::pair{"emptychild y;", ""}, {"emptychild 12 y;", "12"},
        {"emptychild y 12;", "12"}}) {
    valid(parser, text);
    const auto result = parser.parse(text);
    const auto* node = result.value_as<tunordered::Record>();
    CHECK(node != nullptr && node->flag && node->child != nullptr);
    if (node && node->child)
      CHECK(node->child->name == name && node->child->parent == node);
  }
  valid(parser, "wide a b c d e f g h;", "abcdefgh");
  valid(parser, "wide h g f e d c b a;", "hgfedcba");
  valid(parser, "wide c a h e b g d f;", "cahebgdf");

  for (const auto text :
       {"maybe a a;", "maybe a b a;", "maybe b b;", "maybe b a b;",
        "gather a b a;", "gather b a b;", "gather a a b a;",
        "modifiers;", "modifiers static;", "modifiers static final;",
        "modifiers 12 12;", "modifiers static static 12;",
        "modifiers 12 final final;", "emptychild;", "emptychild y y;",
        "emptychild 12 34 y;",
        "order;", "order 12;", "order 12 34;", "order y y;",
        "order 12 34 56 y;", "order y 12 34 56;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }
  for (const auto text :
       {"pair a;", "pair b;", "pair a a;", "pair a b a;", "four a b c;",
        "four a a c d;", "blocks b;", "blocks a b a;", "bounded a b;",
        "bounded a b a;", "bounded a a a b;", "nested a c b;", "prefix x a;",
        "duplicate a;", "optional a;", "tight [a b];", "edges a b ;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }

  CHECK(offers(parser, "fields flag ", "name") &&
        offers(parser, "fields flag ", "value") &&
        !offers(parser, "fields flag ", "flag"));
  CHECK(offers(parser, "modifiers ", "static") &&
        offers(parser, "modifiers ", "final"));
  CHECK(offers(parser, "modifiers static ", "final") &&
        !offers(parser, "modifiers static ", "static"));
  CHECK(offers(parser, "maybe a ", "b") && !offers(parser, "maybe a ", "a"));
}

// A text several orders read goes to the first by written positions, even where
// a later one gives an optional item something to read.
void order_ties(const cursiv::Parser& parser) {
  struct Case {
    std::string_view text, first, second;
  };
  for (const auto& c :
       {Case{"order y;", "", ""}, {"order y 12;", "", "12"},
        {"order 12 y;", "12", ""}, {"order 12 y 34;", "12", "34"},
        {"order 12 34 y;", "12", "34"}, {"order y 12 34;", "12", "34"}}) {
    const auto result = parser.parse(c.text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* node = result.value_as<tunordered::Record>();
    CHECK(node != nullptr);
    if (node)
      CHECK(node->flag && node->first == c.first && node->second == c.second);
  }
}

void nullable_nests(const cursiv::Parser& parser) {
  const auto root = [&](std::string_view text) {
    auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    return result;
  };
  const auto expression = [](const cursiv::ParseResult& result) {
    const auto* node = result.value_as<tunordered::Record>();
    CHECK(node != nullptr);
    return node ? node->expression : nullptr;
  };
  const auto parsed = root("maybenests 12;");
  const auto* bare = cursiv::ast_cast<tunordered::Name>(expression(parsed));
  CHECK(bare != nullptr && bare->name == "12");
  for (const std::string_view ops : {"!", "?"}) {
    const std::string text = "maybenests 12" + std::string(ops) + ";";
    const auto result = root(text);
    const auto* wrap = cursiv::ast_cast<tunordered::Wrap>(expression(result));
    CHECK(wrap != nullptr && wrap->op == ops);
    if (!wrap) continue;
    const auto* atom = cursiv::ast_cast<tunordered::Name>(wrap->inner);
    CHECK(atom != nullptr && atom->name == "12");
  }
  for (const std::string_view ops : {"!?", "?!"}) {
    const std::string text = "maybenests 12" + std::string(ops) + ";";
    const auto result = root(text);
    const auto* outer = cursiv::ast_cast<tunordered::Wrap>(expression(result));
    CHECK(outer != nullptr && outer->op == ops.substr(1));
    if (!outer) continue;
    const auto* inner = cursiv::ast_cast<tunordered::Wrap>(outer->inner);
    CHECK(inner != nullptr && inner->op == ops.substr(0, 1));
    if (!inner) continue;
    const auto* atom = cursiv::ast_cast<tunordered::Name>(inner->inner);
    CHECK(atom != nullptr && atom->name == "12");
  }
  for (const auto text :
       {"maybenests 12!!;", "maybenests 12!?!;", "maybenests 12??;"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }
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
        "child flag node 12;", "optional b a;", "tight [ba];",
        "wrapwords 12(?)(!);", "edges ba ;", "fields value 34 flag name 12;",
        "four d b c a ;", "blocks b a a;", "bounded b a a;",
        "wide c a h e b g d f;", "pair a a;", "nested a c b;", "maybe;",
        "maybe b a;", "maybe a a;", "gather b b a a;",
        "modifiers final 12 static;", "emptychild y;", "emptychild y 12;",
        "order y 12;", "order 12 34 y;", "order y 12 34;", "order 12;",
        "maybenests 12!;", "maybenests 12?!;"})
    alike(*ll, *lr, *llAst, *lrAst, text, true);
  alike(*ll, *lr, *llAst, *lrAst, "maybenests 12;", false);
}

// A loop inside an operand is decided in the operand's own rule: `+1` after
// `name` is a label, as it is where no `&` stands.
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

// Three nests in one group: every order of them wraps the name in the order
// the text names them.
void nests_in_every_order() {
  namespace g = tunorderedturns::generated;
  const auto ll = g::Ll::create_parser();
  const auto lr = g::Lr::create_parser();
  const auto llAst = g::Ll::create_ast_reflection();
  const auto lrAst = g::Lr::create_ast_reflection();
  std::string ops = "!?~";
  do {
    const std::string text = "*a" + ops;
    alike(*ll, *lr, *llAst, *lrAst, text, true);
    for (const auto* parser : {ll.get(), lr.get()}) {
      const auto result = parser->parse(text);
      CHECK(result.accepted && result.diagnostics.empty());
      const auto* doc = result.value_as<tunorderedturns::Doc>();
      CHECK(doc != nullptr);
      if (!doc) continue;
      const tunorderedturns::Expr* at = doc->expression;
      for (std::size_t i = ops.size(); i-- > 0;) {
        const auto* wrap = cursiv::ast_cast<tunorderedturns::Wrap>(at);
        CHECK(wrap != nullptr);
        if (!wrap) break;
        CHECK(wrap->op == std::string_view(&ops[i], 1));
        at = wrap->inner;
      }
      const auto* name = cursiv::ast_cast<tunorderedturns::Name>(at);
      CHECK(name != nullptr && name->name == "a");
    }
  } while (std::next_permutation(ops.begin(), ops.end()));
  for (const auto text : {"*a!?", "*a!!?~", "*a~?"})
    alike(*ll, *lr, *llAst, *lrAst, text, false);
}

void items_reading_nothing(const cursiv::Parser& parser) {
  for (const auto text :
       {"endof a", "endrule a", "implied a", "implied a ;", "implied ; a",
        "negated a", "negated a b", "empty", "empty a"}) {
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
  }
  for (const auto text : {"endof a b", "endof", "endrule a b", "implied",
                          "negated b a", "empty a a"}) {
    const auto result = parser.parse(text);
    CHECK(!result.accepted || !result.diagnostics.empty());
  }
}

}  // namespace

int main() {
  records_alike();
  loop_in_operand();
  nests_in_every_order();
  const auto ll = tunordered::generated::Ll::create_parser();
  const auto lr = tunordered::generated::Lr::create_parser();
  exercise(*ll);
  exercise(*lr);
  lowering_paths(*ll);
  lowering_paths(*lr);
  order_ties(*ll);
  order_ties(*lr);
  nullable_nests(*ll);
  nullable_nests(*lr);
  items_reading_nothing(*tunordered::generated::EndsLl::create_parser());
  spelt<tunordered::Eleven>();
  spelt<tunordered::ElevenOptional>();
  spelt<tunordered::SixNests>();
  refused_too_wide<tunordered::Twelve>();
  refused_too_wide<tunordered::SevenNests>();
  refused_too_wide<tunordered::TooWide>();
  refused_too_wide<tunordered::TooWideNests>();
  return cursiv::test::report("Unordered_test");
}
