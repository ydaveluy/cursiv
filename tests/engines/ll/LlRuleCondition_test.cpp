#include <algorithm>
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "termcondlr/generated/Lr/Module.hpp"
#include "termcondlr/generated/Lr/Parser.hpp"
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/RuleConditionGrammar.hpp"
#include "rcondll/generated/Ll/Module.hpp"
#include "rcondnest/generated/Nested/Module.hpp"

namespace rule_condition_cases {
namespace {

void expect(bool ok, const std::string& what) {
  if (ok) return;
  std::printf("FAIL %s\n", what.c_str());
  ++failures;
}

// The items read, `name name(tag)` each, or `<refused>`.
std::string items(const cursiv::Parser& p, std::string_view text,
                  std::string_view prefix = "rules:") {
  const std::string source = std::string(prefix) + std::string(text);
  const auto r = p.parse(source);
  const auto* d = r.value_as<tllrcond::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || d == nullptr) return "<refused>";
  std::string out;
  for (const tllrcond::Item* i : d->items) {
    for (const std::string_view n : i->names) out += std::string(n) + ' ';
    if (!i->tag.empty()) out += '(' + std::string(i->tag) + ')';
    out += ';';
  }
  return out;
}

// Whether the LL parser reads this text cleanly.
bool reads(const cursiv::Parser& ll, const std::string& text) {
  return items(ll, text) != "<refused>";
}

// The hidden leaves of the tree a parse of `text` keeps.
unsigned hidden(const cursiv::Parser& p, std::string_view text) {
  const std::string source = "rules:" + std::string(text);
  const auto r = p.parse(source);
  unsigned n = 0;
  for (cursiv::NodeId i = 0; i < r.cst().size(); ++i)
    n += r.cst()[i].hidden ? 1u : 0u;
  return n;
}

// Whether a head leaf reads the token of `ikw<"ab"> + lit<".">`: a body of
// four steps in a row, classes and a refusal, opening on `a` of either case.
template <class Cfg>
bool reads_ab_dot(const Cfg& g) {
  for (unsigned l = 0; l < g.ngapLeaves; ++l) {
    if (g.gapLeaves[l].kind != cursiv::GapLeafKind::head) continue;
    const auto operand = g.lex[g.gapLeaves[l].node];
    if (operand.kind != cursiv::LexKind::token) continue;
    std::vector<cursiv::LexNode> steps;
    const auto walk = [&](auto&& self, std::uint16_t n) -> bool {
      const auto e = g.lex[n];
      if (e.kind == cursiv::LexKind::chars ||
          e.kind == cursiv::LexKind::nguard) {
        steps.push_back(e);
        return true;
      }
      if (e.kind != cursiv::LexKind::seq) return false;
      for (unsigned i = 0; i < e.b; ++i)
        if (!self(self, g.lexKid[e.a + i])) return false;
      return true;
    };
    if (walk(walk, operand.b) && steps.size() == 4 &&
        steps[0].kind == cursiv::LexKind::chars &&
        g.sets[steps[0].a].has('a') && g.sets[steps[0].a].has('A'))
      return true;
  }
  return false;
}

template <class E>
struct named;
template <class R>
struct named<cursiv::Ref<R>> {
  using type = R;
};
using Inline =
    named<std::remove_cvref_t<decltype(tllrcond::Base::DotNum)>>::type;

}  // namespace

namespace terminal_condition {

namespace {

// The items read, or `<refused>`.
std::string items(const cursiv::Parser& p, std::string_view text,
                  std::string_view prefix = {}) {
  const std::string source = std::string(prefix) + std::string(text);
  const auto r = p.parse(source);
  const auto* d = r.value_as<tllterm::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || d == nullptr) return "<refused>";
  std::string out;
  for (const tllterm::Item* i : d->items) out += std::string(i->text) + ' ';
  return out;
}

// Whether the completion at the end of `text` offers the terminal `name`.
bool offers(const cursiv::Parser& p, std::string_view text,
            std::string_view name, std::string_view prefix = {}) {
  const std::string source = std::string(prefix) + std::string(text);
  const auto r = p.parse(source);
  const auto at = static_cast<cursiv::TextOffset>(source.size());
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  for (const auto& f : out)
    if (p.getGrammar().terminalName(f.feature) == name) return true;
  return false;
}

// The members the completion at the end of `text` names for `name`.
std::vector<std::string_view> members(const cursiv::Parser& p,
                                      std::string_view text,
                                      std::string_view name,
                                      std::string_view prefix = {}) {
  const std::string source = std::string(prefix) + std::string(text);
  const auto r = p.parse(source);
  const auto at = static_cast<cursiv::TextOffset>(source.size());
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  std::vector<std::string_view> found;
  for (const auto& f : out)
    if (p.getGrammar().terminalName(f.feature) == name)
      found.push_back(f.property);
  return found;
}

}  // namespace

void check_terminal_conditions() {
  const auto ll = tllguardcases::generated::Ll::create_parser();
  const auto lr = tllterm::generated::Lr::create_parser();
  for (const auto& [text, want] : {
           std::pair{"no x1;", "x1 "},
           std::pair{"no 12;", "<refused>"},
           std::pair{"is ab;", "ab "},
           std::pair{"is iffy;", "iffy "},
           std::pair{"is if;", "if "},
           std::pair{"is a.b;", "<refused>"},
           std::pair{"bare x;", "x "},
           std::pair{"bare #x y;", "<refused>"},
           std::pair{"tie letter;", "letter "},
           std::pair{"tie let;", "<refused>"},
           std::pair{"pick let;", "let "},
           std::pair{"pick letter;", "letter "},
           std::pair{"pick x;", "x "},
           std::pair{"group { a; b };", " "},
           std::pair{"group { a b };", "<refused>"},
           std::pair{"line\nfoo = bar;", "foo "},
           std::pair{"line foo = bar;", "<refused>"},
           std::pair{"pair ab;", " "},
           std::pair{"pair nb;", " "},
           std::pair{"pair x;", "<refused>"},
       }) {
    CHECK(items(*ll, text, "terms:") == want);
    CHECK(items(*lr, text) == want);
  }
  // A completion asks the terminal it would insert of the condition, its
  // body where no spelling stands for it.
  for (const cursiv::Parser* p : {ll.get(), lr.get()}) {
    const std::string_view prefix = p == ll.get() ? "terms:" : "";
    CHECK(offers(*p, "bare ", "NAME", prefix) && !offers(*p, "bare ", "NOTE", prefix));
    CHECK(offers(*p, "no ", "NAME", prefix) && !offers(*p, "no ", "NUM", prefix));
    CHECK(offers(*p, "is ", "DOTTED", prefix));
    // A name glued to a keyword reads as one word with it; after `{` it
    // stands alone.
    CHECK(!offers(*p, "no", "NAME", prefix));
    CHECK(offers(*p, "group {", "NAME", prefix));
  }
  // Each word a reading admits is asked: the member is the one every reading
  // fills, and the two branches of `pair` fill two.
  CHECK(members(*ll, "pair ", "NAME", "terms:") == std::vector<std::string_view>{""});
  // A caret measures a phrase that opens on its gap from where the gap
  // begins: it reads on to the `;` standing where its last name would.
  {
    const std::string_view text = "line\nfoo = ;";
    cursiv::TextOffset seen = 0;
    for (unsigned k = 0; k < 256; ++k)
      seen = std::max(seen, tllterm::generated::Lr::caret_seen(
                                text, k, 4, cursiv::Token{5, 8, 0}));
    CHECK(seen > 11);
  }
  // A terminal's body that reads a run to any length is one token, and no
  // run of terminals is endless here.
  CHECK(cursiv::LrLowering<tllterm::Lr>::table().cfg.unbounded.empty());

}
}

void run() {
  terminal_condition::check_terminal_conditions();
  const auto llp = tllguardcases::generated::Ll::create_parser();
  const cursiv::Parser& ll = *llp;
  const auto read = [&](const std::string& text, bool want) {
    expect(reads(ll, text) == want,
           text + (want ? " should read" : " should be refused"));
  };
  const auto in = [](std::string_view w, std::initializer_list<const char*> s) {
    for (const char* x : s)
      if (w == x) return true;
    return false;
  };
  const auto words = {"a", "b", "c", "e", "x", "ax", "bx", "cx", "ex", "A",
                      "B", "Ax"};

  // A run of refusals is the refusal of their union, & of a choice a
  // choice of &, cased or not, and before an optional name.
  for (const char* w : words) {
    const std::string t = std::string(" ") + w + ";";
    const bool abc = in(w, {"a", "b", "c"});
    read("r1" + t, !abc);
    read("u1" + t, !abc);
    read("r1and" + t, abc);
    read("u1and" + t, abc);
    const bool ab = in(w, {"a", "b", "A", "B"});
    read("r1i" + t, !ab);
    read("u1i" + t, !ab);
    read("r1opt" + t, !in(w, {"a", "b"}));
  }
  read("r1opt;", true);

  // A sequence reads its elements with the skipper between them.
  for (const auto& [text, want] : {
           std::pair{"r2 a b;", false},
           std::pair{"r2 a  /*c*/ b;", false},
           std::pair{"r2 a/*c*/b;", false},
           std::pair{"r2 a\nb;", false},
           std::pair{"r2 a c;", true},
           std::pair{"r2 ab;", true},
           std::pair{"r2 b a;", true},
           std::pair{"r2 a;", true},
           std::pair{"r2 ax b;", true},
           std::pair{"r2 a bx;", true},
           std::pair{"r2 a b c;", false},
           std::pair{"r2 x a b;", true},
           std::pair{"r2and a b;", true},
           std::pair{"r2and a /*c*/\n b x;", true},
           std::pair{"r2and a c;", false},
           std::pair{"r2and ab;", false},
           std::pair{"r2and a;", false},
           std::pair{"r2and ax b;", false},
           std::pair{"r2i A b;", false},
           std::pair{"r2i a B;", false},
           std::pair{"r2i A /*c*/ B;", false},
           std::pair{"r2i A c;", true},
           std::pair{"r2i AB;", true},
           std::pair{"r2opt;", true},
           std::pair{"r2opt a;", true},
           std::pair{"r2opt a b;", false},
           std::pair{"r2opt a x;", true},
           std::pair{"r2rep b a;", false},
           std::pair{"r2rep b b /*c*/ b a;", false},
           std::pair{"r2rep b x a;", true},
           std::pair{"r2rep a b;", true},
           std::pair{"r2rep b;", true},
           std::pair{"r2dash [a-b];", false},
           std::pair{"r2dash [-a---b];", false},
           std::pair{"r2dash [a-c];", true},
           std::pair{"r2dash [x-b];", true},
           std::pair{"r2dash [ax-b];", true},
           std::pair{"r2dash [a b];", false},
       })
    read(text, want);
  // A comment that nests is skipped whole.
  const auto nest = tllrcond::generated::Nested::create_parser();
  CHECK(items(*nest, "a {- x {- y -} z -} b;", {}) == "<refused>");
  CHECK(items(*nest, "a {- x {- b -} -} c;", {}) == "a c ;");
  CHECK(items(*nest, "a {- -} {--} b;", {}) == "<refused>");
  // A hidden comment between the two stays in the tree.
  CHECK(hidden(ll, "r2 a /*k*/ c;") == 1);

  // Each alternative owes the refusals in front of it and its own.
  for (const char* w : words) {
    const std::string name = std::string(" ") + w;
    const bool ab = in(w, {"a", "b"});
    read("r3" + name + "(;", !ab && !in(w, {"c"}));
    read("r3" + name + "=;", !ab && !in(w, {"e"}));
    read("r3" + name + ";", !ab);
    const bool lead = in(w, {"a", "c", "e", "x"});
    read("r3and" + name + "(;", lead && !in(w, {"c"}));
    read("r3and" + name + "=;", lead && in(w, {"e"}));
    read("r3and" + name + ";", lead && !in(w, {"a"}));
  }
  // A terminal's text, read as one element: the token in hand whole, a longer
  // token is another; in a sequence, the skipper around it.
  for (const auto& [text, want] : {
           std::pair{"d1 12.;", false},     std::pair{"d1 12.x;", true},
           std::pair{"d1 1 2.;", true},     std::pair{"d1 12;", true},
           std::pair{"d1and 12.;", true},   std::pair{"d1and 12.x;", false},
           std::pair{"d1and 1 2.;", false}, std::pair{"d1and 12;", false},
           std::pair{"d2 12. x;", false},   std::pair{"d2 12./*c*/x;", false},
           std::pair{"d2 12. y;", true},    std::pair{"d2 12 x;", true},
           std::pair{"d2 12.;", true},      std::pair{"d3 ab.;", false},
           std::pair{"d3 AB.x;", true},     std::pair{"d3 abc.;", true},
           std::pair{"d3 ab;", true},       std::pair{"d3 ab .;", true},
           std::pair{"d4 12;", false},      std::pair{"d4 12 x;", false},
           std::pair{"d4 12x;", true},      std::pair{"d4 12.;", true},
           std::pair{"d4 x12;", true},      std::pair{"d5 12.;", false},
           std::pair{"d5 zz;", false},      std::pair{"d5 zzz;", true},
           std::pair{"d5 1 2.;", true},     std::pair{"n1 12.;", false},
           std::pair{"n1 1 2.;", true},
       })
    read(text, want);
  // A data rule reads as the phrase its body spells: nested, keywords with
  // their boundary, a `skip()` in it with its own trivia.
  for (const auto& [text, want] : {
           std::pair{"q1 a;", false},
           std::pair{"q1 a.b;", true},
           std::pair{"q1 12;", true},
           std::pair{"q2 a.b;", true},
           std::pair{"q2 a . b;", false},
           std::pair{"q2 a /*c*/ . b;", false},
           std::pair{"q2 a.b.c;", true},
           std::pair{"q2 a\n.\nb;", false},
           std::pair{"q2 a;", true},
           std::pair{"q2 a b;", true},
           std::pair{"q2 a.;", true},
           std::pair{"q2 a.1;", true},
           std::pair{"q2and a . b;", true},
           std::pair{"q2and a;", false},
           std::pair{"i1 is not;", false},
           std::pair{"i1 is /*c*/ not x;", false},
           std::pair{"i1 is;", true},
           std::pair{"i1 isnot;", true},
           std::pair{"i1 is notx;", true},
           std::pair{"i1 not is;", true},
           std::pair{"i2 is not;", false},
           std::pair{"i2 zz;", false},
           std::pair{"i2 is zz;", true},
           std::pair{"t1 isnot;", true},
           std::pair{"t1 is not;", true},
           std::pair{"t2 a b.c;", false},
           std::pair{"t2 a b..c;", false},
           std::pair{"t2 a b c;", true},
           std::pair{"t2 a.b.c;", true},
       })
    read(text, want);

  // A named terminal reads as the inline one does.
  for (const char* t : {" 12.;", " 12.x;", " 1 2.;", " 12;", " 12. x;",
                        " 12./*c*/x;", " 12. y;", " 12 x;", " zz;", " zzz;"})
    for (const auto& [named, inline_] :
         {std::pair{"n1", "d1"}, std::pair{"n1and", "d1and"},
          std::pair{"n2", "d2"}, std::pair{"n5", "d5"}})
      read(named + std::string(t), reads(ll, inline_ + std::string(t)));

  const auto llb =
      cursiv::ll_build_program(cursiv::LlLowering<tllrcond::Ll>::lower());
  const auto lrb =
      cursiv::lr_build_automaton(cursiv::LrLowering<tllrcond::Lr>::table());
  // An inline terminal keeps its complete lexical body.
  CHECK(reads_ab_dot(llb.cfg));
  CHECK(reads_ab_dot(lrb.cfg));
  // An inline terminal in a condition is no token of the grammar.
  using cursiv::detail::tl_has;
  CHECK((!tl_has<Inline, cursiv::LlLowering<tllrcond::Ll>::terminals>::value));
  CHECK((!tl_has<Inline, cursiv::LrLowering<tllrcond::Lr>::terminals>::value));
  CHECK((tl_has<tllrcond::Base::ENUM_LEAD,
                cursiv::LlLowering<tllrcond::Ll>::terminals>::value));
  // ...nor is a data rule it reads a rule, nor the trivia its `skip()` names.
  using LlRules = cursiv::LlLowering<tllrcond::Ll>::rules;
  using LrRules = cursiv::LrLowering<tllrcond::Lr>::rules;
  CHECK((!tl_has<tllrcond::Base::Dotted, LlRules>::value &&
         !tl_has<tllrcond::Base::QName, LlRules>::value &&
         !tl_has<tllrcond::Base::DOTS, LlRules>::value));
  CHECK((!tl_has<tllrcond::Base::Dotted, LrRules>::value &&
         !tl_has<tllrcond::Base::QName, LrRules>::value &&
         !tl_has<tllrcond::Base::DOTS, LrRules>::value));
}

}  // namespace rule_condition_cases
