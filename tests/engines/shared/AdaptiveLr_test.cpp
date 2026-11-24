#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/engine/grammar/Lower.hpp"
#include "cursiv/engine/grammar/View.hpp"
#include "cursiv/engine/grammar/WritesOnce.hpp"
#include "cursiv/engine/parser/Pack.hpp"

namespace {

using namespace cursiv::engine::gen;

struct Postfix : cursiv::Extends<cursiv::AstNode> {
  Postfix* child = nullptr;
  bool marked = false;
};

struct SharedGroup : cursiv::GrammarSpec {
  static constexpr auto modifiers = unordered(kw<"public">, kw<"static">, kw<"final">);
  struct Once : rule<create<Postfix>(modifiers)> {};
  struct Twice : rule<create<Postfix>(modifiers + lit<";"> + modifiers)> {};
};

template <class R>
struct ModifierGrammar : SharedGroup { using entry = R; };

template <class R>
auto modifier_counts() {
  using G = ModifierGrammar<R>;
  auto image = grammar_image<View<G>>();
  const auto cfg = lower(image, kLowerDesc<G>);
  return std::pair{cfg.nonterminals, cfg.productions.size()};
}

struct UnorderedWrites : cursiv::GrammarSpec {
  struct Root : rule<create<Postfix>(lit<"x">) +
                     unordered(flag<&Postfix::marked>(lit<"?">),
                               nest<&Postfix::child>(flag<&Postfix::marked>(lit<"!">)))> {};
  using entry = Root;
};

struct SharedMarks : cursiv::GrammarSpec {
  struct Tag {};
  static constexpr auto markTag = mark<Tag>(lit<"#">);
  static constexpr auto first = &(lit<"a"> + &same<Tag>);
  static constexpr auto second = &(lit<"b"> + &same<Tag>);

  template <auto Arms>
  struct Grammar : cursiv::GrammarSpec {
    struct Token : terminal<Arms + some(any)> {};
    struct Root : rule<ref<Token>> {};
    using entry = Root;
  };
};

template <auto Arms>
bool shared_capture_is_defined() {
  using G = SharedMarks::Grammar<Arms>;
  auto image = grammar_image<View<G>>();
  const auto cfg = lower(image, kLowerDesc<G>);
  LexAutomatonBuilder lexer(image);
  try {
    for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t) lexer.add(t, cfg.terminals[t]);
    return true;
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("not set on every path") != std::string_view::npos);
    return false;
  }
}

struct OverlappingRuns : cursiv::GrammarSpec {
  struct WS : terminal<some(lit<" "> | lit<"a!">)> {};
  using ignored = WS;
  struct X : terminal<some(lit<"ax"> | lit<"bx">)> {};
  struct Y : terminal<some(lit<"ay"> | lit<"by">)> {};
  struct Z : terminal<some(lit<"cz"> | lit<"dz">)> {};
  struct Root : rule<create<Postfix>(ref<X> | ref<Z> | ref<Y>)> {};
  using entry = Root;
};

void overlapping_runs_keep_their_own_matcher_entry(bool reversed) {
  auto image = grammar_image<View<OverlappingRuns>>();
  const auto cfg = lower(image, kLowerDesc<OverlappingRuns>);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t i = 1; i < cfg.terminals.size(); ++i) {
    const auto t = static_cast<std::uint16_t>(reversed ? cfg.terminals.size() - i : i);
    lexer.add(t, cfg.terminals[t]);
  }
  const auto dfa = lexer.build(cfg.terminals.size());
  const auto lead = dfa.next[dfa.nclasses + dfa.classOf['a']];
  const auto& branches = dfa.acceptSets[dfa.acceptSet[lead]];
  CHECK(branches.size() == 2);
  const auto space = dfa.next[lead * dfa.nclasses + dfa.classOf['!']];
  const auto& ignored = dfa.acceptSets[dfa.acceptSet[space]];
  CHECK(ignored.size() == 1);
  for (const auto& a : ignored)
    CHECK(a.guard == cursiv::engine::LexTest::none && cfg.terminals[a.term].name == "WS");
  const auto c = dfa.next[dfa.nclasses + dfa.classOf['c']];
  const auto z = dfa.next[c * dfa.nclasses + dfa.classOf['z']];
  CHECK(dfa.acceptSets[dfa.acceptSet[c]].empty());
  const auto& direct = dfa.acceptSets[dfa.acceptSet[z]];
  CHECK(direct.size() == 1);
  for (const auto& branch : direct) {
    CHECK(cfg.terminals[branch.term].name == "Z");
    CHECK(branch.guard == cursiv::engine::LexTest::none);
  }
  const auto& m = dfa.matcher;
  for (const auto& branch : branches) {
    CHECK(branch.guard == cursiv::engine::LexTest::match);
    const auto a = m.next[m.entries.at(branch.waits) * m.nclasses + m.classOf['a']];
    const auto x = m.next[a * m.nclasses + m.classOf['x']];
    const auto y = m.next[a * m.nclasses + m.classOf['y']];
    CHECK(!m.accepts[x].empty() == (cfg.terminals[branch.term].name == "X"));
    CHECK(!m.accepts[y].empty() == (cfg.terminals[branch.term].name == "Y"));
  }
}

// A name that is no number and no capitalised word: its condition reads on
// without bound, and its ends wait on it in several ways.
struct UnboundedCondition : cursiv::GrammarSpec {
  static constexpr auto NameChar = cr<"^ ,">;
  struct WS : terminal<some(lit<" ">)> {};
  using ignored = WS;
  struct Name : terminal<!((some(d) | (cr<"A-Z"> + many(cr<"a-z">))) + !NameChar) +
                         some(NameChar)> {};
  struct Root : rule<create<Postfix>(some(ref<Name>))> {};
  using entry = Root;
};

void an_unbounded_condition_is_read_by_one_matcher_thread() {
  using cursiv::engine::LexTest;
  using cursiv::engine::MatchAction;
  auto image = grammar_image<View<UnboundedCondition>>();
  const auto cfg = lower(image, kLowerDesc<UnboundedCondition>);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t) lexer.add(t, cfg.terminals[t]);
  const auto dfa = lexer.build(cfg.terminals.size());
  CHECK(std::ranges::any_of(dfa.acceptSets, [&](const auto& set) {
    return std::ranges::any_of(set, [&](const auto& a) {
      return a.guard == LexTest::match && cfg.terminals[a.term].name == "Name";
    });
  }));
  // Each end of the name is weighed where its thread stands: none forks.
  CHECK(std::ranges::all_of(dfa.matcher.exits,
                            [](const auto& e) { return e.action == MatchAction::end; }));
}

struct MarkedSection : cursiv::GrammarSpec {
  struct Root : rule<create<Postfix>(flag<&Postfix::marked>(lit<"x">))> {};
  using entry = Root;
};

struct SectionWrites : cursiv::GrammarSpec {
  struct Root : rule<section<MarkedSection>(lit<"<">, lit<">">) + flag<&Postfix::marked>(lit<"!">)> {};
  using entry = Root;
};

struct Binary : cursiv::Extends<cursiv::AstNode> {
  Binary* child = nullptr;
  Binary* right = nullptr;
  std::string_view op;
};

struct AliasedOperands : cursiv::GrammarSpec {
  struct Primary : rule<create<Binary>(lit<"name">)> {};
  struct E : infix<Primary, Binary, left(lit<"+">), &Binary::child, &Binary::op, &Binary::child> {};
  using entry = E;
};

struct NullableLead : cursiv::GrammarSpec {
  struct E;
  struct Prefix : rule<option(lit<"prefix:">)> {};
  struct MaybePrefix : rule<ref<Prefix>> {};
  struct Primary : rule<create<Binary>(lit<"name">) |
      create<Binary>(ref<MaybePrefix> + assign<&Binary::child>(ref<E>) + lit<"?">)> {};
  struct E : infix<Primary, Binary, left(lit<"+">), &Binary::child, &Binary::op, &Binary::right> {};
  using entry = E;
};
static_assert(cursiv::engine::Facts<NullableLead>::stage::graph::cyclic<NullableLead::E>);

template <class G>
void rejects_second_write(std::string_view message) {
  auto image = grammar_image<View<G>>();
  try {
    check_writes_once(image, kLowerDesc<G>);
    CHECK(false);
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find(message) != std::string_view::npos);
  }
}

template <auto Word>
struct Embedded : cursiv::GrammarSpec {
  struct Pair : rule<lit<"x"> + lit<"y">> {};
  struct Root : rule<ref<Pair> + Word> {};
  using entry = Root;
};

struct Closings : cursiv::GrammarSpec {
  struct Ends : terminal<lit<"END"> | kw<"STOP">> {};
  static constexpr auto payload = [](std::string_view text) -> std::size_t { return text.starts_with('!') ? 1u : 0u; };
  struct Mixed : terminal<lit<"ab"> | (lit<"@"> + scan<payload>)> {};
};

template <class Inner, auto Close>
struct SectionHost : cursiv::GrammarSpec {
  static constexpr bool case_insensitive = true;
  struct Root : rule<create<Postfix>(lit<"ÉND!more"> | section<Inner>(lit<"<">, Close)) + eof> {};
  using entry = Root;
};

template <class G>
Packed plain_tables() {
  auto image = grammar_image<View<G>>();
  auto cfg = lower(image, kLowerDesc<G>);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t) lexer.add(t, cfg.terminals[t]);
  auto dfa = lexer.build(cfg.terminals.size());
  const auto rivals = lex_rivals(image, cfg, dfa);
  const auto lr = build_lr1(cfg, rivals.takes, Bits(cfg.terminals.size()));
  return pack(image, cfg, std::move(dfa), rivals.takes, lr);
}

struct UnitChains : cursiv::GrammarSpec {
  struct Base : rule<lit<"a"> | lit<"b"> | lit<"c"> | lit<"d">> {};
  struct Alias : rule<ref<Base>> {};
  struct Outer : rule<ref<Alias>> {};
  struct Root : rule<lit<"("> + ref<Outer> + lit<")">> {};
  using entry = Root;
};

void long_unit_chains_share_blocks_and_read_in_order() {
  Packed p;
  for (std::uint32_t i = 1; i <= 256; ++i) p.hops.push_back({i - 1, i});
  for (std::uint32_t i = 257; i <= 512; ++i) p.hops.push_back({256, i});
  auto& sites = p.gotos.emplace_back();
  for (const auto i : {15u, 16u, 17u, 255u, 256u}) sites.push_back({0, 0, kNone, i});
  for (std::uint32_t i = 257; i <= 512; ++i) sites.push_back({0, 0, kNone, i});
  std::vector<std::uint32_t> lists;
  const auto words = p.unitWords(lists);
  cursiv::engine::Tables tables{};
  tables.unitWord = words.data();
  CHECK(words.size() < 8 * p.hops.size());
  for (const auto& site : sites) {
    const auto list = lists[site.hops], depth = std::min(site.hops, 257u);
    CHECK(words[list] == depth);
    for (std::uint32_t i = 0; i < depth; ++i)
      CHECK(tables.unitProduction(list, i) == (i == 256 ? site.hops : i + 1));
  }
}

template <auto Word, auto Close = cursiv::GrammarSpec::lit<"ÉND!">>
bool embedded_word_overlaps_close() {
  using G = SectionHost<Embedded<Word>, Close>;
  try {
    (void)plain_tables<G>();
    return false;
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("embedded grammar also reads") != std::string_view::npos);
    return true;
  }
}

void barring_a_terminal_preserves_the_kernel_origins() {
  Bits lookahead(129), barred(65);
  lookahead.set(64);
  lookahead.set(128);
  barred.set(64);
  lookahead.remove(barred);
  CHECK(!lookahead.test(64) && lookahead.test(128));
}

// Independent LR(1) choices sharing a core: aAd | bAe | aBe | bBd,
// A -> c, B -> c. Each copy needs its own context split.
Cfg choices(unsigned copies) {
  Cfg cfg;
  cfg.nonterminals = 2 + 3 * copies;
  cfg.terminals.resize(2 + 5 * copies);
  cfg.regions.emplace_back();
  const auto add = [&](std::uint32_t lhs, std::vector<Symbol> rhs) {
    Production p;
    p.lhs = lhs;
    p.rhs = std::move(rhs);
    cfg.productions.push_back(std::move(p));
  };
  add(0, {nonterminal(1)});
  for (unsigned i = 0; i < copies; ++i) {
    const Symbol a = 2 + 5 * i, b = a + 1, c = a + 2, d = a + 3, e = a + 4;
    const auto s = 2 + 3 * i, x = s + 1, y = s + 2;
    add(1, {nonterminal(s)});
    add(s, {a, nonterminal(x), d});
    add(s, {b, nonterminal(x), e});
    add(s, {a, nonterminal(y), e});
    add(s, {b, nonterminal(y), d});
    add(x, {c});
    add(y, {c});
  }
  cfg.productionsOf.resize(cfg.nonterminals);
  for (std::uint32_t p = 0; p < cfg.productions.size(); ++p)
    cfg.productionsOf[cfg.productions[p].lhs].push_back(p);
  return cfg;
}

std::size_t conflicts(const LrAutomaton& lr, std::size_t terminals) {
  std::size_t n = 0;
  std::vector<unsigned> actions(terminals);
  for (const auto& state : lr.states) {
    std::ranges::fill(actions, 0);
    for (const auto& [t, target] : state.shifts) ++actions[t];
    for (const auto& [production, on] : state.reduces)
      on.each([&](std::size_t t) { ++actions[t]; });
    for (const auto count : actions) n += count > 1;
  }
  return n;
}

// An optional prefix crosses from its caller's trivia scope into a tight rule.
std::size_t split_size(unsigned length) {
  Cfg cfg;
  cfg.terminals.resize(3);
  cfg.regions.resize(2);
  cfg.nonterminals = 3;
  cfg.productionsOf = {{0}, {1}, {2, 3}};
  cfg.productions.resize(4);
  cfg.productions[0].rhs = {nonterminal(1)};
  auto& body = cfg.productions[1];
  body.lhs = 1;
  body.region = 1;
  body.rhs.assign(length, nonterminal(2));
  body.rhs.push_back(2);
  cfg.productions[2].lhs = cfg.productions[3].lhs = 2;
  cfg.productions[2].rhs = {1};
  split_leads(cfg);
  std::size_t symbols = 0;
  for (const Production& p : cfg.productions) symbols += p.rhs.size();
  return symbols;
}

void a_ceiling_refines_choices_without_exceeding_it() {
  constexpr unsigned copies = 8;
  const Cfg cfg = choices(copies);
  const std::vector<RivalRow> rivals(cfg.terminals.size());
  const Bits trivia(cfg.terminals.size());
  const auto full = build_lr1(cfg, rivals, trivia);
  CHECK(conflicts(full, cfg.terminals.size()) == 0);
  const auto cores = static_cast<std::uint32_t>(full.states.size()) - copies;
  std::size_t previous = 2 * copies;
  for (auto limit = cores; limit <= full.states.size() + 1; ++limit) {
    const auto lr = build_lr1(cfg, rivals, trivia, limit);
    const auto left = conflicts(lr, cfg.terminals.size());
    CHECK(lr.states.size() <= limit);
    CHECK(left <= previous);
    previous = left;
    if (limit == cores) CHECK(left == 2 * copies);
    if (limit >= full.states.size()) CHECK(left == 0);
  }
  const auto fits = build_lr1(cfg, rivals, trivia, 10000);
  CHECK(fits.states.size() == full.states.size());
  for (std::size_t i = 0; i < full.states.size(); ++i) {
    CHECK(fits.states[i].shifts == full.states[i].shifts);
    CHECK(fits.states[i].reduces == full.states[i].reduces);
    CHECK(fits.states[i].gotos == full.states[i].gotos);
    CHECK(fits.states[i].doubted == full.states[i].doubted);
  }
  bool refused = false;
  try {
    (void)build_lr1(cfg, rivals, trivia, cores - 1);
  } catch (const Refused&) {
    refused = true;
  }
  CHECK(refused);
}

void a_shared_reader_needs_a_terminal_that_separates_its_arms() {
  Cfg cfg;
  cfg.terminals.resize(5);
  Packed p;
  p.modeCount = 1;
  p.modeTies.resize(1);
  p.states.resize(4);
  using enum PackedAction::Kind;
  p.states[0].actions = {{1, shift, 1}, {1, shift, 2}};
  p.states[1].actions = {{3, shift, 3}};
  p.states[2].actions = {{2, shift, 3}, {3, shift, 3}};
  const auto predict = [&] {
    PrefixPredictions out;
    prefix_predictions(out, cfg, p, 0, 8, p.states[0].actions, false);
    return out;
  };
  CHECK(predict().plans.size() == 1);
  cfg.newline = 2;
  const auto tied = predict();
  CHECK(tied.cells.empty() && tied.plans.empty() && tied.shifts.empty());
  p.states[1].actions.push_back({4, shift, 3});
  const auto apart = predict();
  CHECK(apart.plans.size() == 1 && apart.shifts.size() == 2);
  for (const auto& [row, id] : apart.shifts) CHECK(row[cfg.newline] == 1);
}

}  // namespace

int main() {
  long_unit_chains_share_blocks_and_read_in_order();
  const auto chains = plain_tables<UnitChains>();
  CHECK(chains.hops.size() == 7);  // Empty chain, two outer rules, four base alternatives.
  using M = SharedMarks;
  constexpr auto first = M::markTag + M::first;
  constexpr auto second = M::markTag + M::second;
  constexpr auto missing = M::lit<"!"> + M::second;
  CHECK((shared_capture_is_defined<first | second>()));
  CHECK((!shared_capture_is_defined<first | missing>()));
  CHECK((!shared_capture_is_defined<missing | first>()));
  overlapping_runs_keep_their_own_matcher_entry(false);
  overlapping_runs_keep_their_own_matcher_entry(true);
  an_unbounded_condition_is_read_by_one_matcher_thread();
  CHECK(modifier_counts<SharedGroup::Once>() == modifier_counts<SharedGroup::Twice>());
  rejects_second_write<UnorderedWrites>("'marked' twice");
  rejects_second_write<AliasedOperands>("'child' twice");
  rejects_second_write<SectionWrites>("'marked' twice");
  using G = cursiv::GrammarSpec;
  CHECK(!embedded_word_overlaps_close<G::lit<"z">>());
  CHECK(embedded_word_overlaps_close<G::lit<"énd!word">>());
  CHECK((!embedded_word_overlaps_close<G::lit<"ENDING">, G::kw<"END">>()));
  CHECK((embedded_word_overlaps_close<G::lit<"END!">, G::kw<"END">>()));
  CHECK((embedded_word_overlaps_close<G::lit<"END">, G::ref<Closings::Ends>>()));
  CHECK((!embedded_word_overlaps_close<G::lit<"STOPPED">, G::ref<Closings::Ends>>()));
  CHECK((!embedded_word_overlaps_close<G::lit<"a", &G::lit<"b">>, G::lit<"ab">>()));
  CHECK((embedded_word_overlaps_close<G::lit<"ab", &G::lit<"!">>, G::lit<"ab">>()));
  CHECK((embedded_word_overlaps_close<G::ref<Closings::Mixed>, G::lit<"ab">>()));
  using Mixed = Embedded<G::ref<Closings::Mixed>>;
  CHECK((plain_tables<SectionHost<Mixed, G::lit<"@!">>>().warnings.size() == 1));
  CHECK((plain_tables<SectionHost<Mixed, G::lit<"az">>>().warnings.empty()));
  barring_a_terminal_preserves_the_kernel_origins();
  CHECK(split_size(128) <= 2 * split_size(64));
  a_ceiling_refines_choices_without_exceeding_it();
  a_shared_reader_needs_a_terminal_that_separates_its_arms();
  return cursiv::test::report("AdaptiveLr_test");
}
