#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "../shared/GapReference.hpp"
#include "asicases/generated/G/Parser.hpp"
#include "asicases/generated/G/Table.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/gen/LrAmbiguity.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/test/Check.hpp"
#include "door/generated/G/Module.hpp"
#include "engines/lr/AsiGrammar.hpp"
#include "engines/lr/DoorGrammar.hpp"
#include "engines/lr/PassesGrammar.hpp"
#include "passes/generated/G/Module.hpp"

namespace lr_lowered_cases {

// Grammars lowered in process: where an infix step rides, how a directive
// distributes, and the boundary a region hands back.

// The token an infix operator opens on carries the level's step. A rule whose
// leading item can vanish opens on the next one too, so reading position zero
// alone binds the step to the wrong token -- and the shift side of the cell is
// then left with nothing to compare.
namespace infix_first {

namespace {

struct Bin : cursiv::Extends<cursiv::AstNode> {
  Bin* left = nullptr;
  cursiv::AstNode::string_view op;
  Bin* right = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  // The operator is a rule whose head may match nothing: `*` is the token it
  // opens on in every document that does not spell `@`.
  struct Pre : rule<option(lit<"@">), cursiv::AstNode::string_view> {};
  struct Star : rule<ref<Pre> + lit<"*">, cursiv::AstNode::string_view> {};
  struct Atom : rule<assign<&Bin::op>(ref<NUM>)> {};
  struct Expr : infix<Atom, &Bin::left, &Bin::op, &Bin::right,
                      (left(ref<Star>) >> left(lit<"+">))> {};
  using entry = Expr;
  using ignore = WS;
};

}  // namespace

int run() {
  const cursiv::LrLowered low = cursiv::LrLowering<G>::table();
  CHECK(!low.cfg.lowerFail());
  CHECK(!low.fault);
  unsigned stepped = 0;
  for (unsigned t = 1; t < low.cfg.nterms; ++t) {
    const std::string_view sp = low.cfg.term_text(t);
    const std::uint8_t prec = low.cfg.termBuild[t].prec;
    if (prec != 0) ++stepped;
    std::printf("  %-4.*s prec=%u\n", static_cast<int>(sp.size()),
                sp.empty() ? "" : sp.data(), prec);
    // `*` opens the tighter level, `+` the looser one: both carry a step.
    if (sp == "*" || sp == "+") CHECK(prec != 0);
  }
  CHECK(stepped >= 2);
  return 0;
}

}  // namespace infix_first

// A directive over a choice is one directive per alternative, so each branch
// keeps its own conversion. A region between the two changes nothing: it is
// the shape, and the shape is not what distributes.
namespace distribute {

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  double v = 0;
};

constexpr auto ten = [](std::string_view t) {
  double x = 0;
  for (const char c : t) x = x * 10 + (c - '0');
  return x;
};
constexpr auto pct = [](std::string_view t) {
  double x = 0;
  for (const char c : t)
    if (c != '%') x = x * 10 + (c - '0');
  return x / 100;
};

struct Bare : cursiv::GrammarSpec {
  struct NUM : terminal<some(d), cursiv::text_conv<ten>> {};
  struct PCT : terminal<some(d) + lit<"%">, cursiv::text_conv<pct>> {};
  struct Root : rule<create<Doc>(assign<&Doc::v>(ref<PCT> | ref<NUM>))> {};
  using entry = Root;
};

struct Regioned : cursiv::GrammarSpec {
  struct NUM : terminal<some(d), cursiv::text_conv<ten>> {};
  struct PCT : terminal<some(d) + lit<"%">, cursiv::text_conv<pct>> {};
  struct Root : rule<create<Doc>(assign<&Doc::v>(
      tight(ref<PCT> | ref<NUM>)))> {};
  using entry = Root;
};

// How many distinct chains the productions of the entry name: one per
// alternative when the directive distributed, one for both when it did not.
template <class G>
unsigned sources(const char* name) {
  const cursiv::LrLowered low = cursiv::LrLowering<G>::table();
  CHECK(!low.cfg.lowerFail());
  unsigned seen = 0;
  std::uint16_t rows[8] = {};
  for (std::uint16_t q = 0; q < low.cfg.nprods; ++q) {
    const cursiv::LrLowProd& p = low.cfg.prods[q];
    if (p.lhs != low.cfg.entryNt || p.len == 0) continue;
    const std::uint16_t r = low.cfg.row_at(p, 0);
    if (r == cursiv::kNoRow) continue;
    bool had = false;
    for (unsigned k = 0; k < seen; ++k) had = had || rows[k] == r;
    if (!had && seen < 8) rows[seen++] = r;
  }
  std::printf("  %-10s %u distinct chain(s) over the entry's positions\n", name,
              seen);
  return seen;
}

}  // namespace

int run() {
  const unsigned bare = sources<Bare>("bare");
  const unsigned regioned = sources<Regioned>("regioned");
  CHECK(bare == 2);
  // A region between the directive and the choice is not a reason for the two
  // alternatives to share one conversion.
  CHECK(regioned == bare);
  return 0;
}

}  // namespace distribute

// The boundary a region hands back to its caller stands at one point, so it
// answers once: both productions of `aux -> aux X | X` read it off their last
// position, and the empty reading of a rule closing the region stands past
// it, as the inline form's does.
namespace region_tail {

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Doc>(
      tight(lit<"["> + some(ref<ID>)) + lit<"#">)> {};
  using entry = Root;
  using ignore = WS;
};

// A region closing on something that may match nothing: the point after `c`
// is inside the region for `a` and past it for the empty reading.
struct Inline : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Doc>(
      lit<"x"> + tight(lit<"c"> + option(lit<"a">)) +
      lit<"y">)> {};
  using entry = Root;
  using ignore = WS;
};
struct Called : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct R : rule<option(lit<"a">)> {};
  struct Root : rule<create<Doc>(
      lit<"x"> + tight(lit<"c"> + ref<R>) + lit<"y">)> {};
  using entry = Root;
  using ignore = WS;
};
struct Committed : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Doc>(
      lit<"x"> + tight(lit<"c"> + option(commit(lit<"a">, lit<"b">))) +
      lit<"y">)> {};
  using entry = Root;
  using ignore = WS;
};

// The same tail under trivia of its own, and nested regions that both close
// on a nullable tail: refused alike. A region naming the grammar's own trivia
// is no boundary at all.
struct Trivia : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(lit<"\n">)> {};
  struct Root : rule<create<Doc>(
                    lit<"x"> +
                    skip(ignored<WS, NL>, hide<>, lit<"c"> + option(lit<"a">)) +
                    lit<"y">)> {};
  using entry = Root;
  using ignore = WS;
};
struct Nested : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CM : terminal<lit<"//"> + many(cr<R"(^\n)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto Inner =
      skip(ignored<WS>, hide<>, ref<ID> + option(lit<"!">));
  struct Item : rule<skip(ignored<WS, CM>, hide<>, Inner + option(lit<"?">))> {
  };
  struct Root : rule<create<Doc>(lit<"m"> + ref<Item> + option(lit<"end">))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<CM>;
};
struct Same : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<Doc>(
            lit<"x"> + skip(ignored<WS>, hide<>, lit<"c"> + option(lit<"a">)) +
            lit<"y">)> {};
  using entry = Root;
  using ignore = WS;
};

template <class H>
bool refused_at_the_region_edge() {
  const cursiv::LrBuildTable a =
      cursiv::lr_build_automaton(cursiv::LrLowering<H>::table());
  return a.cfg.lowerFail() &&
         a.cfg.lowerWhy == cursiv::LowerFail::regionBoundaryShared;
}

}  // namespace

int run() {
  const cursiv::LrLowered low = cursiv::LrLowering<G>::table();
  CHECK(!low.cfg.lowerFail());
  // The auxiliary the repetition became: the only nonterminal with a
  // production that names itself.
  unsigned aux = 0xffffu;
  for (std::uint16_t q = 0; q < low.cfg.nprods; ++q) {
    const cursiv::LrLowProd& p = low.cfg.prods[q];
    for (unsigned k = 0; k < p.len; ++k)
      if (!cursiv::is_term(low.cfg.slotSym[p.at + k]) &&
          cursiv::nt_index(low.cfg.slotSym[p.at + k]) == p.lhs)
        aux = p.lhs;
  }
  CHECK(aux != 0xffffu);
  // Every production of it closes on the same boundary.
  unsigned seen = 0, tight = 0;
  for (std::uint16_t q = 0; q < low.cfg.nprods; ++q) {
    if (low.cfg.prods[q].lhs != aux) continue;
    ++seen;
    tight += low.cfg.prodBuild[q].tailCfg != 0 ? 1u : 0u;
  }
  std::printf("  %u productions for the run, %u of them tight at the tail\n",
              seen, tight);
  CHECK(seen >= 2);
  CHECK(tight == 0 || tight == seen);
  CHECK(refused_at_the_region_edge<Inline>());
  CHECK(refused_at_the_region_edge<Called>());
  CHECK(refused_at_the_region_edge<Committed>());
  CHECK(refused_at_the_region_edge<Trivia>());
  CHECK(refused_at_the_region_edge<Nested>());
  CHECK(!refused_at_the_region_edge<Same>());
  return 0;
}

}  // namespace region_tail

// A rule only a region reaches is read there, where a skipped terminal is a
// token of its own: the reading outside is never entered, so it refuses
// nothing. A rule read outside too does name the terminal where no text can
// match it.
namespace region_rule {

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Inside : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Pair : rule<ref<ID> + ref<WS> + ref<ID>> {};
  struct Root : rule<create<Doc>(tight(lit<"["> + ref<Pair> + lit<"]">))> {};
  using entry = Root;
  using ignore = WS;
};

struct Both : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Pair : rule<ref<ID> + ref<WS> + ref<ID>> {};
  struct Root : rule<create<Doc>((lit<"p"> + ref<Pair>) |
                                 tight(lit<"["> + ref<Pair> + lit<"]">))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace

int run() {
  const cursiv::LrLowered& inside = cursiv::LrLowering<Inside>::table();
  CHECK(!inside.fault);
  CHECK(!cursiv::lr_build_automaton(inside).cfg.lowerFail());
  const cursiv::LrLowered& both = cursiv::LrLowering<Both>::table();
  CHECK(both.fault.code == cursiv::GrammarFaultCode::skippedTerminal);
  CHECK(both.cfg.nt_name(both.fault.rule) == "Pair");
  return 0;
}

}  // namespace region_rule

// A run inside a run keeps the cuts it was written with, which the table keeps
// open; `option(some(x))` is one run.
namespace nested_repeat {

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Flat : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>(many(ref<X>) + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};
struct ManySome : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>(many(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};
struct OptionSome : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>(option(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};
struct Run : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Xs : rule<some(ref<X>)> {};
  struct Root : rule<create<Doc>(many(ref<Xs>) + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};

template <class G>
cursiv::LrBuildTable built() {
  return cursiv::lr_build_automaton(cursiv::LrLowering<G>::table());
}

}  // namespace

int run() {
  const cursiv::LrBuildTable flat = built<Flat>();
  CHECK(flat.nconflicts == 0);
  const cursiv::LrBuildTable single = built<OptionSome>();
  CHECK(single.nconflicts == 0);
  CHECK(single.nstates == flat.nstates && single.cfg.nprods == flat.cfg.nprods);
  CHECK(built<ManySome>().nconflicts != 0);
  CHECK(built<Run>().nconflicts != 0);
  return 0;
}

}  // namespace nested_repeat

// A section closed on the start of a token its grammar reads: where it ends
// would be that grammar's reading. One closed on a token none of its own
// begins with is read. A grammar that embeds itself closes its section on
// its own token.
namespace section_end {

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Inner : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Doc>(lit<"a"> + option(lit<">=">))> {};
  using entry = Root;
  using ignore = WS;
};

// A grammar embedding itself: the inner section closes on a token the
// grammar reads.
struct Self : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<Doc>(lit<"a">) | section<Self>(lit<"(">, lit<")">)> {};
  using entry = Root;
  using ignore = WS;
};

template <cursiv::str Close>
struct Outer : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<section<Inner>(lit<"<">, lit<Close>)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace

int run() {
  const cursiv::LrBuildTable prefix =
      cursiv::lr_build_automaton(cursiv::LrLowering<Outer<">">>::table());
  CHECK(prefix.cfg.lowerWhy == cursiv::LowerFail::sectionEndShared);
  const cursiv::LrBuildTable apart =
      cursiv::lr_build_automaton(cursiv::LrLowering<Outer<"/">>::table());
  CHECK(apart.cfg.lowerWhy == cursiv::LowerFail::none);
  const cursiv::LrBuildTable self =
      cursiv::lr_build_automaton(cursiv::LrLowering<Self>::table());
  CHECK(self.cfg.lowerWhy == cursiv::LowerFail::sectionEndShared);
  return 0;
}

}  // namespace section_end

namespace case_lrambiguity {
// The ambiguity the lowered grammar shows with no table: two runs of one item
// side by side. What reads its text one way stays silent. And a condition that
// reads a run to any length, which completion cannot step over.

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Y : terminal<lit<"y">> {};
  using ignore = WS;
};

// Two runs of one item side by side, or with only an option between them.
struct Twice : Base {
  struct Root : rule<create<Doc>(many(ref<X>) + many(ref<X>) + lit<";">)> {};
  using entry = Root;
};
struct Between : Base {
  struct Root : rule<create<Doc>(many(ref<X>) + option(ref<Y>) + some(ref<X>) +
                                 lit<";">)> {};
  using entry = Root;
};

// Each of these reads its text one way.
struct Flat : Base {
  struct Root : rule<create<Doc>(many(ref<X>) + lit<";">)> {};
  using entry = Root;
};
struct Words : Base {
  struct Word : rule<ref<X> + lit<"w">> {};
  struct Root : rule<create<Doc>(many(ref<Word>) + lit<";">)> {};
  using entry = Root;
};
struct Apart : Base {
  struct Root
      : rule<create<Doc>(many(ref<X>) + ref<Y> + many(ref<X>) + lit<";">)> {};
  using entry = Root;
};
struct Others : Base {
  struct Root : rule<create<Doc>(many(ref<X>) + many(ref<Y>) + lit<";">)> {};
  using entry = Root;
};
// A chain that begins on another item is no run: `Head X X` is one `List`.
struct Chain : Base {
  struct Head : rule<lit<"h">> {};
  struct List : rule<(ref<List> + ref<X>) | ref<Head>> {};
  struct Root : rule<create<Doc>(many(ref<List>) + lit<";">)> {};
  using entry = Root;
};

// A condition that reads digits to any length, twice in one spelling, then
// the same cut to a prefix.
struct Endless : Base {
  struct Item
      : rule<!(some(cr<"0-9">) + lit<"."> + some(cr<"0-9">)) + ref<X>> {};
  struct Root : rule<create<Doc>(many(ref<Item>) + lit<";">)> {};
  using entry = Root;
};
struct EndlessDashes : Base {
  struct Arrow : rule<!(some(lit<"-">) + lit<">">)+ref<X>> {};
  struct Root : rule<create<Doc>(ref<Arrow> + lit<";">)> {};
  using entry = Root;
};
struct EndlessMany : Base {
  struct Item : rule<!(many(cr<"0-9">) + lit<".">)+ref<X>> {};
  struct Root : rule<create<Doc>(ref<Item> + lit<";">)> {};
  using entry = Root;
};
struct EndlessSeveral : Base {
  struct Item
      : rule<!(repeat<3, kUnbounded>(cr<"0-9">) + lit<".">)+ref<X>> {};
  struct Root : rule<create<Doc>(ref<Item> + lit<";">)> {};
  using entry = Root;
};
// Whole terminals, each a run of its own: a token apiece, and no end to
// nothing but the repetition of terminals.
struct Tokens : Base {
  struct Word : terminal<some(cr<"a-z">)> {};
  struct Item : rule<!(ref<Word> + kw<"in">)+ref<X>> {};
  struct Root : rule<create<Doc>(many(ref<Item>) + lit<";">)> {};
  using entry = Root;
};
struct EndlessTokens : Base {
  struct Word : terminal<some(cr<"a-z">)> {};
  struct Item : rule<!(many(ref<Word>) + lit<".">)+ref<X>> {};
  struct Root : rule<create<Doc>(many(ref<Item>) + lit<";">)> {};
  using entry = Root;
};
struct Bounded : Base {
  struct Item : rule<!(repeat<1, 9>(cr<"0-9">) + lit<".">) + ref<X>> {};
  struct Root : rule<create<Doc>(many(ref<Item>) + lit<";">)> {};
  using entry = Root;
};

template <class G>
const cursiv::LrBuildCfg& cfg() {
  return cursiv::LrLowering<G>::table().cfg;
}

template <class G>
std::vector<cursiv::LrAmbiguity> found() {
  return cursiv::lr_find_ambiguities(cfg<G>());
}

template <class G>
std::string unbounded_text() {
  const auto& c = cfg<G>();
  return cursiv::unbounded_condition_text(c, c.nt_name(c.unbounded[0].rule),
                                          c.unbounded[0]);
}

template <class G>
bool says(const char* part) {
  const auto all = found<G>();
  return !all.empty() &&
         cursiv::lr_ambiguity_text(cfg<G>(), all[0]).find(part) !=
             std::string::npos;
}

}  // namespace

int run() {
  CHECK(found<Twice>().size() == 1 && found<Between>().size() == 1);
  CHECK(says<Twice>("rule `Root` reads a run of `X` twice in a row"));
  CHECK(says<Between>("a run of `X` twice"));
  CHECK(found<Flat>().empty());
  CHECK(found<Words>().empty());
  CHECK(found<Apart>().empty());
  CHECK(found<Others>().empty());
  CHECK(found<Chain>().empty());
  CHECK(cfg<Endless>().unbounded.size() == 1 &&
        unbounded_text<Endless>().find(
            "rule `Item` asks a condition of the token in hand that "
            "reads `[0-9]+` to any length") != std::string::npos);
  CHECK(cfg<EndlessDashes>().unbounded.size() == 1 &&
        unbounded_text<EndlessDashes>().find(
            "rule `Arrow` asks a condition of the token in hand "
            "that reads `('-')+` to any length") != std::string::npos);
  CHECK(cfg<EndlessMany>().unbounded.size() == 1 &&
        unbounded_text<EndlessMany>().find("reads `[0-9]*` to any length") !=
            std::string::npos);
  CHECK(cfg<EndlessSeveral>().unbounded.size() == 1 &&
        unbounded_text<EndlessSeveral>().find(
            "reads `[0-9]{3,}` to any length") != std::string::npos);
  CHECK(cfg<Bounded>().unbounded.empty());
  CHECK(cfg<Tokens>().unbounded.empty());
  CHECK(cfg<EndlessTokens>().unbounded.size() == 1);
  return 0;
}
}  // namespace case_lrambiguity

namespace case_lrloweredmodules {
// Grammars both lowered in process and generated: what the lowering keeps is
// what the generated parser reads.

// A state's fast path probes the bytes it can shift on without materialising
// a token. An open cell is not one it may answer alone: the reading the cell
// keeps beside the shift is a reading the drive still owes, and bypassing it
// refuses texts the grammar states.
namespace named_conflict {

int run() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tnamedconf::G>::table());
  // The cell is open and nothing was ordered away: what the parser reads is
  // the fast path's alone to get right.
  CHECK(A.nconflicts != 0);
  CHECK(A.nOrdered == 0);
  const auto engine = tdoor::generated::G::create_parser();
  // `S := A ID ID` over `A := ID | ID ID`, so the language is three names or
  // four and nothing else. The three-name reading is the one the open cell
  // keeps beside its shift.
  const struct {
    const char* text;
    bool states;
  } cases[] = {{"", false},     {"a", false},       {"a b", false},
               {"a b c", true}, {"a b c d", true},  {"a b c d e", false}};
  for (const auto& c : cases) {
    const std::string text = "named " + std::string(c.text);
    const cursiv::ParseResult r = engine->parse(text);
    const bool real = r.accepted && r.diagnostics.empty();
    if (real == c.states) continue;
    std::printf("FAIL: the parser %s `%s`, the grammar %s\n",
                real ? "reads" : "refuses", c.text,
                c.states ? "states it" : "does not");
    ++failures;
  }
  return 0;
}

}  // namespace named_conflict

// The second reading a region asks for is dynamic: the island mints its clones
// and the table, which no region reaches, is lowered once and climbs normally.
namespace infix_region {

namespace {

namespace ast = tinfr::ast;
using G = tinfr::G;
using Low = cursiv::LrLowering<G>;

// The last nonterminal of a name is its clone, and a clone is what the second
// reading is asked for by.
std::uint16_t clone_of(std::string_view n) {
  const auto& c = Low::cfg();
  std::uint16_t r = 0;
  for (std::uint16_t i = 1; i < c.nnts; ++i)
    if (c.nt_name(i) == n) r = i;
  return r;
}

void only_what_the_region_reaches_is_read_twice() {
  CHECK(Low::table().fault.code == cursiv::GrammarFaultCode::none);
  CHECK(Low::cfg().cloneNeeded[clone_of("Dotted")]);
  CHECK(!Low::cfg().cloneNeeded[clone_of("Sum")]);
}

const cursiv::Parser& engine() {
  static const std::unique_ptr<cursiv::Parser> p =
      tdoor::generated::G::create_parser();
  return *p;
}

cursiv::ParseResult parse(std::string_view src) {
  cursiv::ParseResult r = engine().parse(src);
  CHECK(r.accepted);
  return r;
}

void the_ladder_still_climbs() {
  cursiv::ParseResult r = parse("region [a.b] 1 + 2 * 3");
  CHECK(static_cast<bool>(r) && r.diagnostics.empty());
  auto* doc = r.value_as<ast::Doc>();
  CHECK(doc != nullptr && doc->items.size() == 2);
  CHECK(cursiv::ast_cast<ast::Key>(doc->items[0]) != nullptr);
  const auto* sum = cursiv::ast_cast<ast::Bin>(doc->items[1]);
  CHECK(sum != nullptr && sum->op == "+");
  CHECK(cursiv::ast_cast<ast::Num>(sum->left) != nullptr);
  const auto* mul = cursiv::ast_cast<ast::Bin>(sum->right);
  CHECK(mul != nullptr && mul->op == "*");
}

void the_island_stays_tight() {
  cursiv::ParseResult r = parse("region [a . b]");
  CHECK(!r.diagnostics.empty());
}

namespace wri {

using G = tinfr::wri::G;

void the_island_hands_its_node_up() {
  CHECK(cursiv::LrLowering<G>::table().fault.code ==
        cursiv::GrammarFaultCode::none);
  cursiv::ParseResult r = engine().parse("wri [a.b] [c]");
  CHECK(r.accepted);
  CHECK(static_cast<bool>(r) && r.diagnostics.empty());
  auto* doc = r.value_as<ast::Doc>();
  CHECK(doc != nullptr && doc->items.size() == 2);
  if (doc == nullptr || doc->items.size() != 2) return;
  const auto* first = cursiv::ast_cast<ast::Key>(doc->items[0]);
  CHECK(first != nullptr && first->head == "a" && first->tail.size() == 1);
  const auto* second = cursiv::ast_cast<ast::Key>(doc->items[1]);
  CHECK(second != nullptr && second->head == "c" && second->tail.empty());
}

}  // namespace wri

}  // namespace

int run() {
  only_what_the_region_reaches_is_read_twice();
  the_ladder_still_climbs();
  the_island_stays_tight();
  wri::the_island_hands_its_node_up();
  return 0;
}

}  // namespace infix_region

// The width of an alternative is not a shape the core refuses: twenty
// positions lower, build and parse.
namespace wide_alternative {

namespace {

using Low = cursiv::LrLowering<twidealt::G>;

void the_lowering_kept_the_twenty() {
  CHECK(Low::table().fault.code == cursiv::GrammarFaultCode::none);
  const auto& c = Low::cfg();
  unsigned widest = 0;
  for (std::uint16_t k = 0; k < c.nprods; ++k)
    widest = std::max<unsigned int>(c.prods[k].len, widest);
  CHECK(widest == 20);
}

void the_parser_reads_it() {
  const std::unique_ptr<cursiv::Parser> p =
      tpasses::generated::G::create_parser();
  const cursiv::ParseResult r =
      p->parse("wide one + - * / % ^ & | ~ < > = : ; , . ? @ two");
  CHECK(r.accepted);
  CHECK(static_cast<bool>(r) && r.diagnostics.empty());
  auto* row = r.value_as<twidealt::ast::Row>();
  CHECK(row != nullptr);
  if (row == nullptr) return;
  CHECK(row->head == "one");
  CHECK(row->tail == "two");
}

}  // namespace

int run() {
  the_lowering_kept_the_twenty();
  the_parser_reads_it();
  return 0;
}

}  // namespace wide_alternative

namespace gap {

int run() {
  using namespace cursiv;
  cursiv::test::count_expectations();
  const auto lowered = LrLowering<tasi::cases::G>::table();
  const auto& g = lowered.cfg;
  std::mt19937 rng(123);
  const std::string alphabet = "abcxyz; \r\n\xe2\x80\xa8\xa9\xce\xb1";
  std::vector<std::string> samples{
      "",        "ab",           "cd",           "xay",
      "\r\n",    "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80",
      "xxxy",    "xxz",          "xxab",         "xxxxab",
      "xxxxxab", "aab",          "xα",           "xαz"};
  for (const unsigned size : {63u, 64u, 65u, 127u, 128u, 129u}) {
    samples.emplace_back(size, 'a');
    samples.emplace_back(std::string(size - 1, 'a') + "b");
    samples.emplace_back(std::string(size - 4, ' ') + "xxab");
  }
  for (unsigned i = 0; i < 2000; ++i) {
    std::string text(rng() % 180, ' ');
    for (auto& ch : text) ch = alphabet[rng() % alphabet.size()];
    samples.push_back(std::move(text));
  }
  Cst tree("");
  CstBuilder builder(tree);
  LrContext c("", builder);
  for (const auto& sample : samples) {
    const std::string text = "prefix" + sample + (rng() % 2 ? ";" : "");
    c.src = text.data();
    c.len = static_cast<TextOffset>(text.size());
    // A context keeps what a condition read of its one text.
    c.gaps.clear();
    const TextOffset from = 6;
    const Token la{.begin = static_cast<TextOffset>(from + sample.size())};
    for (const bool known : {false, true}) {
      std::vector<unsigned> leaves;
      for (unsigned l = 0; l < g.ngapLeaves; ++l) {
        const auto& leaf = g.gapLeaves[l];
        unsigned v = 0;
        switch (leaf.kind) {
          case GapLeafKind::has:
          case GapLeafKind::is:
            v = 2u *
                cursiv::test::gap_reference(
                    g.lex.data(), g.lexKid.data(),
                    cursiv::lex_boundary(tasi::cases::generated::G::kTable.cfg),
                    leaf.node, sample.data(), sample.size(),
                    leaf.kind == GapLeafKind::is);
            break;
          case GapLeafKind::end:
            v = 2u * (known && gap_end(text.size(), la));
            break;
          case GapLeafKind::head: {
            const auto operand = g.lex[leaf.node];
            CHECK(operand.kind == LexKind::token);
            const auto rest = static_cast<TextOffset>(text.size()) - la.begin;
            const cursiv::test::GapReference reader{
                g.lex.data(), g.lexKid.data(),
                cursiv::lex_boundary(tasi::cases::generated::G::kTable.cfg),
                text.data() + la.begin, rest};
            cursiv::test::GapReference::Span first;
            first.set(0);
            v = known ? 2u * !reader.step(operand.b, first).empty() : 1u;
            break;
          }
          case GapLeafKind::phrase:
          case GapLeafKind::cut:
            CHECK(false);
            break;
        }
        leaves.push_back(v);
      }
      for (unsigned k = 0; k < g.ngapConds; ++k) {
        std::vector<unsigned> stack;
        for (auto i = g.gapCondAt[k]; i < g.gapCondAt[k + 1]; ++i) {
          const auto op = g.gapOps[i];
          if (op == kGapNot) {
            ++i;
            stack.back() = 2u - stack.back();
          } else if (op == kGapAnd || op == kGapOr) {
            const auto count = g.gapOps[++i];
            unsigned v = stack.back();
            for (unsigned j = 1; j < count; ++j)
              v = op == kGapAnd ? std::min(v, stack[stack.size() - 1 - j])
                                : std::max(v, stack[stack.size() - 1 - j]);
            stack.resize(stack.size() - count);
            stack.push_back(v);
          } else {
            stack.push_back(leaves[op]);
          }
        }
        CHECK(tasi::cases::generated::G::caret_cond(text, k, from, la, known,
                                                   nullptr) ==
              (stack.back() != 0));
        if (known)
          CHECK(tasi::cases::generated::G::drive_cond(c, k, from, la) ==
                (stack.back() != 0));
      }
    }
  }
  return 0;
}

}  // namespace gap

int run() {
  int status = 0;
  status |= named_conflict::run();
  status |= infix_region::run();
  status |= wide_alternative::run();
  status |= gap::run();
  return 0 | status;
}
}  // namespace case_lrloweredmodules

int run() {
  int movedStatus = 0;
  movedStatus |= case_lrambiguity::run();
  movedStatus |= case_lrloweredmodules::run();
  int status = 0;
  status |= infix_first::run();
  status |= distribute::run();
  status |= region_tail::run();
  status |= region_rule::run();
  status |= nested_repeat::run();
  status |= section_end::run();
  return (cursiv::test::report("LrLowered_test") | status) | movedStatus;
}

}  // namespace lr_lowered_cases
