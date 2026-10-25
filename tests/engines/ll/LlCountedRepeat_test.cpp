#include <cstdio>
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "engines/ll/CountedScopeGrammar.hpp"
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "countedll/generated/Ll/Module.hpp"
#include "countedlr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/gen/LlLowerPool.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/CountedRepeatGrammar.hpp"
using namespace tllcounted;
#include "semanticll/generated/Ll/Module.hpp"

namespace {
struct Huge : cursiv::GrammarSpec {
  struct Root : rule<repeat<65535>(lit<"a">)> {};
  using entry = Root;
};
struct DeepOptionalBound : cursiv::GrammarSpec {
  struct Root : rule<repeat<0, 513>(lit<"a">)> {};
  using entry = Root;
};
struct NoCeiling : cursiv::GrammarSpec {
  struct Root : rule<repeat<0, 5000>(lit<"a">)> {};
  using entry = Root;
};
struct Crowded : cursiv::GrammarSpec {
  struct First : rule<repeat<30000>(lit<"a">)> {};
  struct Second : rule<repeat<30000>(lit<"b">)> {};
  struct Third : rule<repeat<30000>(lit<"c">)> {};
  struct Root : rule<ref<First> + ref<Second> + ref<Third>> {};
  using entry = Root;
};
struct TruncatedBound : cursiv::GrammarSpec {
  struct Root
      : rule<repeat<(std::numeric_limits<std::size_t>::max() - 1)>(lit<"a">)> {
  };
  using entry = Root;
};
struct NullableUnbounded : cursiv::GrammarSpec {
  struct Root : rule<repeat<2, cursiv::kUnbounded>(option(lit<"a">))> {};
  using entry = Root;
};
template <std::size_t Min, std::size_t Max>
struct NullableCounted : cursiv::GrammarSpec {
  struct EmptyItem : rule<create<Item>> {};
  struct Root : rule<create<Range>(
                    repeat<Min, Max>(append<&Range::items>(ref<EmptyItem>)))> {
  };
  using entry = Root;
};
void counted_bounds_are_in_the_predictive_graph() {
  const auto b = cursiv::ll_build_program(cursiv::LlLowering<Base>::lower(),
                                          {.staticPredict = false});
  CHECK(!b.fault && !b.overflow);
  auto states = b.states.v;
  states.push_back({});
  const auto finish = b.nstates;
  const cursiv::LlAtnView atn{.states = states.data(),
                              .nstates = b.nstates + 1,
                              .edges = b.edges.data(),
                              .ruleStart = b.ruleStart.data(),
                              .callerBase = b.callerBase.data(),
                              .callerAt = b.callerAt.data()};
  std::uint16_t a = 0;
  for (std::uint16_t i = 0; i < b.cfg.nterms; ++i)
    if (b.cfg.term_name(i) == "A") a = i;
  CHECK(a != 0);
  for (const auto& expected : {std::pair{"TwoRule", std::pair{2, 2}},
                               {"ThreeRule", {3, 3}},
                               {"Bounded", {2, 4}},
                               {"OptionalCount", {0, 3}},
                               {"Owed", {3, 7}},
                               {"Once", {1, 1}}}) {
    std::uint16_t rule = 0;
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r)
      if (b.cfg.rule_name(r) == expected.first) rule = r;
    CHECK(rule != 0);
    for (int n = 0; n <= 7; ++n) {
      cursiv::LlSimulation sim(atn);
      const auto stack = sim.contexts().push(cursiv::kLlCtxNone, finish);
      sim.seed(b.ruleStart[rule], stack);
      for (int i = 0; i < n; ++i) sim.step(a);
      bool complete = false;
      for (const auto& c : sim.configs())
        complete |= c.state == finish && c.ctx == cursiv::kLlCtxNone;
      CHECK(complete ==
            (n >= expected.second.first && n <= expected.second.second));
    }
  }
  const auto two = cursiv::LlLowering<Base>::cfg();
  std::set<std::uint32_t> returns;
  for (std::uint16_t i = 0; i < two.nnodes; ++i)
    if (two.nodes[i].kind == cursiv::LlShape::call &&
        two.rule_name(two.nodes[i].sym) == "ItemRule")
      CHECK(returns.insert(b.nodeFollow[i]).second);
}
void an_internal_zero_count_preserves_the_outer_action_rows() {
  // The public DSL excludes max==0; the empty normalized shape is still
  // well-defined and must retain its directives when built internally.
  cursiv::LlLowered low;
  cursiv::detail::llow::Pool pool;
  const auto kid = pool.push({cursiv::detail::llow::Shape::none});
  const auto rep = pool.push({cursiv::detail::llow::Shape::rep, kid, 0, 0, 0});
  const cursiv::detail::llow::Acts made{3}, row{7};
  const auto id = cursiv::detail::llow::emit(low, pool, rep, made, row, 0);
  CHECK(low.cfg.nodes[id].kind == cursiv::LlShape::empty);
  const auto chain = low.writes.chains[low.cfg.nodes[id].row];
  CHECK(chain.count == 2 && low.writes.acts[chain.off] == 3 &&
        low.writes.acts[chain.off + 1] == 7);
}

void unreasonable_counts_are_refused_by_name() {
  const auto huge = cursiv::LlLowering<Huge>::lower();
  CHECK(huge.cfg.lowerWhy == cursiv::LowerFail::tooManyPositions);
  CHECK(huge.cfg.rule_name(huge.cfg.lowerRule) == "Root");
  const auto truncated = cursiv::LlLowering<TruncatedBound>::lower();
  CHECK(truncated.cfg.lowerWhy == cursiv::LowerFail::tooManyPositions);
  const auto crowded = cursiv::LlLowering<Crowded>::lower();
  CHECK(crowded.cfg.lowerWhy == cursiv::LowerFail::tooManyPositions);
  CHECK(crowded.cfg.rule_name(crowded.cfg.lowerRule) != "Root");
  const auto spread = cursiv::LlLowering<DeepOptionalBound>::lower();
  CHECK(spread.cfg.lowerWhy == cursiv::LowerFail::none);
  const auto deep = cursiv::ll_build_program(spread);
  CHECK(!deep.fault && !deep.overflow);
  const auto longest = cursiv::ll_build_program(
      cursiv::LlLowering<NoCeiling>::lower(), {.staticPredict = false});
  CHECK(!longest.fault && !longest.overflow &&
        longest.cfg.lowerWhy == cursiv::LowerFail::none);
  CHECK(longest.cfg.nnodes > 2 * 5000);
  cursiv::LlLowered full;
  cursiv::LowerFail why = cursiv::LowerFail::none;
  try {
    for (;;) (void)cursiv::detail::llow::put_node(full, {});
  } catch (const cursiv::LowerRefused& refusal) {
    why = refusal.why;
  }
  CHECK(why == cursiv::LowerFail::tooManyPositions);
  auto unbounded =
      cursiv::ll_build_program(cursiv::LlLowering<NullableUnbounded>::lower());
  CHECK(unbounded.fault.code == cursiv::GrammarFaultCode::nullableRepetition);
  const auto two = cursiv::ll_build_program(
      cursiv::LlLowering<NullableCounted<2, 2>>::lower());
  CHECK(two.fault.code == cursiv::GrammarFaultCode::nullableRepetition);
  const auto upTo = cursiv::ll_build_program(
      cursiv::LlLowering<NullableCounted<0, 3>>::lower());
  CHECK(upTo.fault.code == cursiv::GrammarFaultCode::nullableRepetition);
}
}  // namespace
namespace repeat_scope {
// Counted repetitions beside, inside and under directives, with and without a
// rule called ahead of or behind them: each text has a fixed validity bound.

namespace {

bool clean(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() && r.value != nullptr;
}

std::string join(const auto& words) {
  std::string out;
  for (const std::string_view w : words)
    out += (out.empty() ? "" : ",") + std::string(w);
  return out;
}

std::string shape(const cursiv::ParseResult& r) {
  const auto* doc = r.value_as<tscope::ast::Doc>();
  if (doc == nullptr) return "-";
  std::string out = join(doc->p) + "|";
  for (const auto* item : doc->r) out += std::string(item->text) + ",";
  for (const auto* sub : doc->subs) out += "[" + join(sub->p) + "]";
  return out;
}

struct Engines {
  const cursiv::Parser& ll;

  void read(const std::string& text, bool expected) const {
    const std::string source = "scope:" + text;
    const cursiv::ParseResult parsed = ll.parse(source);
    const bool ok = clean(parsed) == expected;
    if (!ok)
      std::printf("  \"%s\": LL %s %s\n", text.c_str(),
                  clean(parsed) ? "reads" : "repairs", shape(parsed).c_str());
    CHECK(ok);
  }
};

struct Pair {
  const char* key;
  char first;
  unsigned firstLo, firstHi;
  char second;
  unsigned secondLo, secondHi;
};

constexpr Pair kPairs[] = {
#define TSCOPE_ROW(KEY, K1, T1, LO1, HI1, K2, T2, LO2, HI2) \
  {#KEY, T1, LO1, HI1, T2, LO2, HI2},
    TSCOPE_PAIRS(TSCOPE_ROW)
#undef TSCOPE_ROW
};

std::string run(char token, unsigned count) {
  std::string out;
  for (unsigned i = 0; i < count; ++i) out += std::string(" ") + token;
  return out;
}

void two_runs(const Engines& e) {
  for (const Pair& p : kPairs)
    for (unsigned x = 0; x <= p.firstHi + 1; ++x)
      for (unsigned y = 0; y <= p.secondHi + 1; ++y)
        e.read(p.key + run(p.first, x) + run(p.second, y),
               x >= p.firstLo && x <= p.firstHi && y >= p.secondLo &&
                   y <= p.secondHi);
}

void runs_in_other_shapes(const Engines& e) {
  e.read("n01 b", true);
  e.read("n01 a d b", true);
  e.read("n01 a d d a d b b", true);
  e.read("n01 a b", false);
  e.read("n01", false);
  e.read("n01 a d a d a d b", false);
  e.read("o01 b b", true);
  e.read("o01 a b c", true);
  e.read("o01 a d d d b c b b", true);
  e.read("o01 a d d d d b b", false);
  e.read("o01 b", false);
  e.read("m01 b", true);
  e.read("m01 a a ; b", true);
  e.read("m01 a a ; a a a ; b b", true);
  e.read("m01 a ; b", false);
  e.read("a01 x", true);
  e.read("a01 a a x", true);
  e.read("a01 a a a x", true);
  e.read("a01 b b y", true);
  e.read("a01 b b b b y", true);
  e.read("a01 b y", false);
  e.read("a01 a a a a x", false);
  e.read("c01 b b", true);
  e.read("c01 a a b c", true);
  e.read("c01 a a a b b c c", true);
  e.read("c01 a a a a b b", false);
  e.read("d01 b b", true);
  e.read("d01 a a a b c c", true);
  e.read("d01 a a a a b b", false);
  e.read("t01 b", true);
  e.read("t01 a a a b b d d", true);
  e.read("t01 a b d", true);
  e.read("t01 d", false);
  e.read("s01 <i;> b b", true);
  e.read("s01 a a <i;> b c b b", true);
  e.read("s01 b b", false);
  e.read("r01 b b", true);
  e.read("r01 a a b c", true);
  e.read("r01 a a a b b b b", true);
  e.read("r01 a a a a b b", false);
  e.read("g01 bb;", true);
  e.read("g01 aabbb;", true);
  e.read("g01 aaabbbb;", true);
  e.read("g01 a bb;", false);
  e.read("g01 aab b;", false);
  e.read("g01 a;", false);
}

}  // namespace

void check_repeat_scopes() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const Engines e{*ll};
  two_runs(e);
  runs_in_other_shapes(e);

}
}

int main() {
  repeat_scope::check_repeat_scopes();
  counted_bounds_are_in_the_predictive_graph();
  an_internal_zero_count_preserves_the_outer_action_rows();
  unreasonable_counts_are_refused_by_name();
  const auto ll = generated::Ll::create_parser();
  const auto lr = generated::Lr::create_parser();
  const auto run = [](std::string_view prefix, std::size_t n) {
    std::string text(prefix);
    for (std::size_t i = 0; i < n; ++i) text += " a";
    return text + ";";
  };
  std::vector<std::string> texts{
      "c aa;",
      "c aaa;",
      "r aa;",
      "r aaa;",
      "r aaaa;",
      "o;",
      "o a;",
      "o aa;",
      "o aaa;",
      "u aaa;",
      "u aaaaa;",
      "s a;",
      "w a;",
      "p;",
      "p a;",
      "c aa;c aaa;r aa;r aaa;r aaaa;o;o a;o aa;o aaa;u aaa;u aaaaa;s a;"};
  for (const std::size_t n : {0, 1, 2, 19, 39, 40})
    texts.push_back(run("x", n));
  for (const std::size_t n : {3, 4, 15, 29, 30}) texts.push_back(run("y", n));
  texts.push_back(run("x", 39) + run("y", 3) + run("x", 0));
  for (const std::string& text : texts) {
    auto a = ll->parse(text), b = lr->parse(text);
    CHECK(a.accepted && b.accepted && a.diagnostics.empty() &&
          b.diagnostics.empty());
    const auto *x = a.value_as<Document>(), *y = b.value_as<Document>();
    CHECK(x && y);
    if (!x || !y) continue;
    CHECK(x->entries.size() == y->entries.size());
    if (x->entries.size() != y->entries.size()) continue;
    for (std::size_t i = 0; i < x->entries.size(); ++i) {
      const auto *e = x->entries[i], *f = y->entries[i];
      CHECK(e && f && e->type == f->type && e->parent == x && f->parent == y);
      if (!e || !f || e->type != f->type) continue;
      const auto check = [&](const auto* left, const auto* right) {
        CHECK(right != nullptr);
        if (right == nullptr) return;
        CHECK(left->items.size() == right->items.size());
        if (left->items.size() != right->items.size()) return;
        for (std::size_t j = 0; j < left->items.size(); ++j) {
          const auto *l = left->items[j], *r = right->items[j];
          CHECK(l && r && l->text == r->text && l->parent == left &&
                r->parent == right);
          if (!l || !r) continue;
          const auto &ls = a.cst()[l->cstNodeId], &rs = b.cst()[r->cstNodeId];
          CHECK(ls.begin == rs.begin && ls.end == rs.end);
        }
      };
      if (auto* l = cursiv::ast_cast<Two>(e))
        check(l, cursiv::ast_cast<Two>(f));
      else if (auto* l = cursiv::ast_cast<Three>(e))
        check(l, cursiv::ast_cast<Three>(f));
      else if (auto* l = cursiv::ast_cast<Range>(e))
        check(l, cursiv::ast_cast<Range>(f));
      else if (auto* l = cursiv::ast_cast<Around>(e))
        check(l, cursiv::ast_cast<Around>(f));
    }
  }
  for (const std::string_view text :
       {"s a;", "c aa;", "c aaa;", "r aa;", "r aaaa;"}) {
    const auto a = ll->parse(text), b = lr->parse(text);
    for (std::size_t at = 2; at < text.size(); ++at) {
      std::vector<cursiv::NextFeature> x, y;
      ll->nextFeaturesAt(a.cst(), at, at, x);
      lr->nextFeaturesAt(b.cst(), at, at, y);
      const auto features = [](const cursiv::Parser& parser, const auto& list) {
        std::set<
            std::tuple<std::string, std::string, const cursiv::AstTypeTag*>>
            result;
        for (const auto& f : list) {
          auto spelling = parser.getGrammar().terminalText(f.feature);
          if (spelling.empty())
            spelling = parser.getGrammar().terminalName(f.feature);
          result.emplace(spelling, f.property, f.type);
        }
        return result;
      };
      const auto actual = features(*ll, x), expected = features(*lr, y);
      // Public completion has no known call stack for the shared ItemRule.
      // It therefore includes later counted sites as well as this one.
      CHECK(std::includes(actual.begin(), actual.end(), expected.begin(),
                          expected.end()));
    }
  }
  for (std::string_view text :
       {"c a;", "c aaaa;", "r a;", "r aaaaa;", "o aaaa;", "u aa;", "s;",
        "s aa;", "w;", "w aa;", "p aa;"}) {
    auto a = ll->parse(text), b = lr->parse(text);
    CHECK(!a.diagnostics.empty() && !b.diagnostics.empty());
  }
  for (const std::string& text : {run("x", 41), run("y", 2), run("y", 31)}) {
    auto a = ll->parse(text), b = lr->parse(text);
    CHECK(!a.diagnostics.empty() && !b.diagnostics.empty());
  }
  {
    const auto* engine = ll.get();
    const auto items = [&](const std::string& text) -> std::size_t {
      const std::string source = "long:" + text;
      const auto a = engine->parse(source);
      const auto* document = a.value_as<Document>();
      if (!a.accepted || !a.diagnostics.empty() || document == nullptr ||
          document->entries.size() != 1)
        return ~std::size_t{0};
      const auto* range = cursiv::ast_cast<Range>(document->entries[0]);
      return range == nullptr ? ~std::size_t{0} : range->items.size();
    };
    for (const std::size_t n : {0, 1, 2, 511, 512, 513, 599, 600})
      CHECK(items(run("x", n)) == n);
    for (const std::size_t n : {3, 4, 513, 999, 1000})
      CHECK(items(run("y", n)) == n);
    for (const std::string& text : {run("x", 601), run("y", 2), run("y", 1001)})
      CHECK(!engine->parse("long:" + text).diagnostics.empty());
  }
  return cursiv::test::report("LlCountedRepeat_test");
}
