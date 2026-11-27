#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/engine/generator/Generate.hpp"
#include "cursiv/engine/grammar/Lower.hpp"
#include "cursiv/engine/grammar/Optimize.hpp"
#include "cursiv/engine/grammar/View.hpp"
#include "cursiv/engine/grammar/WritesOnce.hpp"
#include "cursiv/engine/parser/EmitDrive.hpp"
#include "engines/shared/ConformanceGrammars.hpp"

namespace {

using namespace cursiv::engine::gen;

struct Postfix : cursiv::Extends<cursiv::AstNode> {
  Postfix* child = nullptr;
  bool marked = false;
};

struct Block : cursiv::Extends<cursiv::AstNode> {
  std::vector<Postfix*> items;
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

// A chain of terminals, each reading the one before, and a terminal that
// reads any of them: each call is followed to the literal once, so the lexer
// is built in time linear in the chain, well within the bound.
void a_chain_of_terminal_calls_is_followed_once() {
  constexpr std::size_t kLinks = 8000;
  const ImageDesc letter{.kind = ImageKind::lit, .text = "a"};
  std::vector<ImageStageDesc> stages(kLinks);
  std::vector<ImageRuleDesc> rules(kLinks);
  std::vector<ImageDesc> calls(kLinks);
  std::vector<const ImageDesc*> arms(kLinks);
  for (std::size_t k = 0; k < kLinks; ++k) {
    stages[k].body = k == 0 ? &letter : &calls[k - 1];
    rules[k] = {.name = "Link", .stage = &stages[k]};
    calls[k] = {.kind = ImageKind::ref, .rule = &rules[k]};
    arms[k] = &calls[k];
  }
  const ImageDesc any{.kind = ImageKind::choice, .count = kLinks, .children = arms.data()};
  const auto start = std::chrono::steady_clock::now();
  GrammarImage image;
  Terminal terminal;
  terminal.name = "Any";
  terminal.body = image.node(any);
  LexAutomatonBuilder lexer(image);
  lexer.add(1, terminal);
  (void)lexer.build(2);
  const auto spent = std::chrono::steady_clock::now() - start;
  if (spent >= std::chrono::seconds(1))
    std::printf("a chain of %zu terminal calls took %lld ms\n", kLinks,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(spent).count()));
  CHECK(spent < std::chrono::seconds(1));
}

// Each word terminal calls the one below through two aliases: the words of a
// choice that several calls reach are listed once, not once per path.
struct DoubledWords : cursiv::GrammarSpec {
  struct Base : terminal<lit<"a"> | lit<"b">> {};
  template <int K>
  struct Word;
  template <int K>
  using Below = std::conditional_t<K == 1, Base, Word<K - 1>>;
  template <int K>
  struct Left : terminal<ref<Below<K>>> {};
  template <int K>
  struct Right : terminal<ref<Below<K>>> {};
  template <int K>
  struct Word : terminal<ref<Left<K>> | ref<Right<K>>> {};
  struct Root : rule<create<Postfix>(ref<Word<16>>)> {};
  using entry = Root;
};

void shared_words_are_listed_once() {
  auto image = grammar_image<View<DoubledWords>>();
  const auto cfg = lower(image, kLowerDesc<DoubledWords>);
  std::size_t most = 0;
  for (const Terminal& t : cfg.terminals) most = std::max(most, t.words.size());
  if (most != 2) std::printf("a terminal of the words a and b lists %zu words\n", most);
  CHECK(most == 2);
}

struct NamedClosers : cursiv::GrammarSpec {
  struct Close : terminal<lit<"*/">> {};
  struct Again : terminal<lit<"*/"> | ref<Again>> {};
  template <auto Closer>
  struct Grammar : cursiv::GrammarSpec {
    struct Comment : terminal<through(lit<"/*">, Closer)> {};
    struct Root : rule<create<Postfix>(ref<Comment> | ref<Close>)> {};
    using entry = Root;
  };
};

// The lexer's table where a comment closes on `Closer`.
template <auto Closer>
std::vector<std::uint16_t> closing_table() {
  using G = NamedClosers::Grammar<Closer>;
  auto image = grammar_image<View<G>>();
  const auto cfg = lower(image, kLowerDesc<G>);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t) lexer.add(t, cfg.terminals[t]);
  return lexer.build(cfg.terminals.size()).next;
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
  std::string_view op;
};

struct AliasedOperands : cursiv::GrammarSpec {
  struct Primary : rule<create<Binary>(lit<"name">)> {};
  struct E : infix<Primary, Binary, left(lit<"+">), &Binary::child, &Binary::op, &Binary::child> {};
  using entry = E;
};

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

// Each inherited set shares its unchanged blocks, even when adjacent rules
// write properties whose numbers are far apart.
void a_chain_of_rules_shares_its_inherited_writes() {
  constexpr std::size_t n = 3000;
  std::vector<cursiv::engine::MemberDesc> members(n);
  std::vector<cursiv::engine::RowDesc> rows(n);
  std::vector<const cursiv::engine::RowDesc*> rowList;
  std::vector<ImageDesc> refs(n), flags(n), seqs(n);
  std::vector<std::array<const ImageDesc*, 2>> parts(n);
  std::vector<ImageStageDesc> stages(n);
  std::vector<ImageRuleDesc> rules(n);
  const ImageDesc word{.kind = ImageKind::lit};
  const ImageDesc* const words[] = {&word};
  for (std::size_t i = 0; i < n; ++i) {
    rows[i].member = &members[i];
    rowList.push_back(&rows[i]);
    flags[i] = {.kind = ImageKind::flag, .count = 1, .children = words, .row = &rows[997 * i % n]};
    if (i > 0) refs[i] = {.kind = ImageKind::ref, .rule = &rules[i - 1]};
    parts[i] = {&refs[i], &flags[i]};
    seqs[i] = {.kind = ImageKind::seq, .count = 2, .children = parts[i].data()};
    stages[i] = {.kind = ImageRuleKind::node, .body = &seqs[i]};
    rules[i].stage = &stages[i];
  }
  GrammarImage image;
  const auto branches = check_writes_once(image, {rowList, &rules.back()});
  CHECK(branches <= 3 * n * (1 + std::bit_width((n - 1) / 64)));

  flags.back().row = flags[1].row;
  members[997].name = "repeated";
  GrammarImage repeated;
  try {
    check_writes_once(repeated, {rowList, &rules.back()});
    CHECK(false);
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("'repeated' twice") != std::string_view::npos);
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

Packed plain_tables(GrammarImage& image, Cfg& cfg) {
  optimize(cfg);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t) lexer.add(t, cfg.terminals[t]);
  auto dfa = lexer.build(cfg.terminals.size());
  const auto rivals = lex_rivals(image, cfg, dfa);
  const auto lr = build_lr1(cfg, rivals.takes, Bits(cfg.terminals.size()));
  return pack(image, cfg, std::move(dfa), rivals.takes, lr);
}

template <class G>
Packed plain_tables() {
  auto image = grammar_image<View<G>>();
  auto cfg = lower(image, kLowerDesc<G>);
  return plain_tables(image, cfg);
}

template <bool Shared>
struct ScopedShift : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<tight(lit<"a"> + lit<"b">) |
                     (lit<"a"> + tight(lit<Shared ? "b" : "c"> + lit<"d">)) |
                     skip(ignore<WS, WS>, hide<>, lit<"x"> + lit<"y">) |
                     (lit<"x"> + lit<"y"> + lit<"z">)> {};
  using entry = Root;
  using ignored = WS;
};

void a_shared_shift_requires_one_trivia_setting() {
  (void)plain_tables<ScopedShift<false>>();
  try {
    (void)plain_tables<ScopedShift<true>>();
    CHECK(false);
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("the trivia before 'b'") != std::string_view::npos);
  }
}

struct WordPrefixes : cursiv::GrammarSpec {
  struct Blank : terminal<some(cr<"a-z">)> {};
  using ignored = Blank;
  struct Word : terminal<lit<"a"> + option(lit<"a">)> {};
  struct Root : rule<ref<Word>> {};
  using entry = Root;
};

std::size_t prefix_lexer_size(unsigned count) {
  auto image = grammar_image<View<WordPrefixes>>();
  auto cfg = lower(image, kLowerDesc<WordPrefixes>);
  auto& word = *std::ranges::find(cfg.terminals, "Word", &Terminal::name);
  const auto start = image.nodes[word.body].first;
  std::vector<ImageId> parts(count, image.children[start + 1]);
  parts.front() = image.children[start];
  word.body = image.group(ImageKind::seq, parts);
  const auto tables = plain_tables(image, cfg);
  std::size_t size = 0;
  emit_drive(cfg, tables, {}, [&](std::string_view name, std::initializer_list<std::string_view> text) {
    if (name == "ParserStates.hpp")
      for (const auto part : text) size += part.size();
  });
  return size;
}

struct Cycles : cursiv::GrammarSpec {
  struct Node : rule<create<Postfix>(option(lit<"a">))> {};
  struct Solid : rule<create<Postfix>(lit<"a">)> {};
  struct SilentList : rule<create<Block>(many(option(lit<"a">)))> {};
  struct NodeList : rule<create<Block>(many(append<&Block::items>(ref<Node>)))> {};
  struct SolidList : rule<create<Block>(many(append<&Block::items>(ref<Solid>)))> {};
  struct NodeOnce : rule<create<Block>(append<&Block::items>(ref<Node>) + lit<";">)> {};
  struct Back;
  struct Unit : rule<ref<Back>> {};
  struct Back : rule<ref<Unit> | lit<"x">> {};
};

template <class R>
struct CyclesGrammar : Cycles { using entry = R; };

// Whether the grammar is refused for a cycle of rules that read no input.
template <class R>
bool a_cycle_of_rules_is_refused() {
  try {
    (void)plain_tables<CyclesGrammar<R>>();
    return false;
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("a cycle of rules reads no input") != std::string_view::npos);
    return true;
  }
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

// Distinct callers converge after the first token of one long continuation.
std::pair<std::size_t, std::size_t> implied_context_size(unsigned length, bool binary = false) {
  Cfg cfg;
  cfg.nonterminals = 5;
  cfg.terminals.resize(binary ? 8 : 5 + 2 * length);
  for (std::size_t t = 1; t < cfg.terminals.size(); ++t)
    cfg.terminals[t].body = 0;
  cfg.regions.emplace_back();
  const auto add = [&](std::uint32_t lhs, std::vector<Symbol> rhs) {
    Production p;
    p.lhs = lhs;
    p.rhs = std::move(rhs);
    cfg.productions.push_back(std::move(p));
  };
  add(0, {nonterminal(1)});
  for (unsigned i = 0; i < length; ++i) {
    std::vector<Symbol> prefix;
    if (binary) {
      for (auto bit = std::bit_width(length - 1); bit-- > 0;)
        prefix.push_back(5 + (i >> bit & 1));
    } else {
      prefix.push_back(5 + 2 * i);
    }
    prefix.push_back(nonterminal(2));
    prefix.push_back(binary ? 7 : 6 + 2 * i);
    add(1, std::move(prefix));
  }
  std::vector<Symbol> continuation(length, 1);
  continuation.push_back(nonterminal(3));
  add(2, std::move(continuation));
  add(3, {2});
  add(3, {nonterminal(4), nonterminal(3), 3});
  add(4, {4});
  add(4, {});
  cfg.productions.back().implied = true;
  cfg.productions.back().tests.emplace_back().gap = Test::Gap::empty;
  cfg.productionsOf.resize(cfg.nonterminals);
  for (std::uint32_t p = 0; p < cfg.productions.size(); ++p)
    cfg.productionsOf[cfg.productions[p].lhs].push_back(p);
  split_leads(cfg);
  std::size_t input = 0;
  for (const auto& p : cfg.productions) input += p.rhs.size();
  const auto lr = build_lr1(cfg, std::vector<RivalRow>(cfg.terminals.size()),
                            Bits(cfg.terminals.size()));
  std::size_t symbols = 0;
  for (const auto& p : cfg.productions) symbols += p.rhs.size();
  CHECK(symbols <= 3 * input);
  return {symbols, lr.states.size()};
}

void check_rebuilding(Cfg& cfg, const std::vector<RivalRow>& rivals, const Bits& trivia) {
  const auto copy_of = [](const Cfg& from) {
    Cfg copy;
    copy.terminals = from.terminals;
    copy.regions = from.regions;
    copy.nonterminals = from.nonterminals;
    copy.productions = from.productions;
    copy.productionsOf = from.productionsOf;
    copy.conditions = from.conditions;
    copy.openings = from.openings;
    copy.recordInputs = from.recordInputs;
    copy.infixes = from.infixes;
    copy.turnContexts = from.turnContexts;
    copy.entries = from.entries;
    copy.rows = from.rows;
    return copy;
  };
  auto retry = copy_of(cfg);
  const auto first = build_lr1(cfg, rivals, trivia);
  const auto normalized = cfg.productions.size();
  auto copy = copy_of(cfg);
  const auto row = [](const LrState& state) {
    return std::tie(state.shifts, state.reduces, state.gotos, state.kernel,
                    state.named, state.follow, state.barred, state.scopes,
                    state.governs, state.behind, state.doubted);
  };
  for (auto* prepared : {&cfg, &copy, &retry}) {
    if (prepared == &retry) {
      bool refused = false;
      try {
        (void)build_lr1(retry, rivals, trivia, 1);
      } catch (const Refused&) {
        refused = true;
      }
      CHECK(refused);
      if (!cfg.turnContexts) CHECK(retry.openings.size() == cfg.openings.size());
    }
    const auto again = build_lr1(*prepared, rivals, trivia);
    CHECK(prepared->productions.size() == normalized);
    CHECK(first.conditions == again.conditions);
    CHECK(first.states.size() == again.states.size());
    for (std::size_t s = 0; s < std::min(first.states.size(), again.states.size()); ++s)
      CHECK(row(first.states[s]) == row(again.states[s]));
  }
}

// R -> a | test_i R suffix_i; positive tests have false marker arms.
void rebuilding_preserves_opening_slots() {
  for (const auto [markers, positive] :
       {std::pair{1u, 0u}, {1u, 1u}, {3u, 2u}, {3u, 5u}}) {
    Cfg cfg;
    cfg.nonterminals = 3 + markers;
    cfg.terminals.resize(3 + 2 * markers);
    for (auto& t : cfg.terminals) t.body = 0;
    cfg.regions.emplace_back();
    const auto add = [&](std::uint32_t lhs, std::vector<Symbol> rhs) {
      auto& p = cfg.productions.emplace_back();
      p.lhs = lhs;
      p.rhs = std::move(rhs);
    };
    add(0, {nonterminal(1)});
    add(1, {nonterminal(2)});
    add(1, {2 + markers, nonterminal(3), 1});
    add(2, {1});
    for (unsigned i = 0; i < markers; ++i) {
      add(2, {nonterminal(3 + i), nonterminal(2), 2 + i});
      add(3 + i, {});
      auto& test = cfg.productions.back().tests.emplace_back();
      test.ahead = {static_cast<std::uint16_t>(3 + markers + i)};
      test.unless = (positive >> i & 1) == 0;
    }
    cfg.productionsOf.resize(cfg.nonterminals);
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p)
      cfg.productionsOf[cfg.productions[p].lhs].push_back(p);
    split_leads(cfg);
    check_rebuilding(cfg, std::vector<RivalRow>(cfg.terminals.size()), Bits(cfg.terminals.size()));
    CHECK(!cfg.turnContexts);
    CHECK(cfg.openings.size() == markers);
    std::uint32_t depth = 0;
    for (const auto& opening : cfg.openings)
      for (const auto& slot : opening) depth = std::max(depth, slot.depth + 1);
    CHECK(depth == markers);
    // The same test still becomes a bar at an ordinary, non-marker site.
    if ((positive & 1) == 0) {
      const auto& plain = cfg.productions[2];
      CHECK((plain.rhs == std::vector<Symbol>{2 + markers, 1}));
      CHECK(plain.bars.size() == 1 && plain.bars.front().first == 1);
    }
  }
}

struct ImpliedExpressions : tconformance::ImpliedPrefix {
  using entry = Expression;
};

void rebuilding_implied_contexts_preserves_infix_decisions() {
  auto image = grammar_image<View<ImpliedExpressions>>();
  auto cfg = lower(image, kLowerDesc<ImpliedExpressions>);
  optimize(cfg);
  LexAutomatonBuilder lexer(image);
  for (std::uint16_t t = 1; t < cfg.terminals.size(); ++t)
    lexer.add(t, cfg.terminals[t]);
  const auto dfa = lexer.build(cfg.terminals.size());
  const auto rivals = lex_rivals(image, cfg, dfa);
  Bits trivia(cfg.terminals.size());
  for (std::size_t t = 1; t < cfg.terminals.size(); ++t)
    if (cfg.terminals[t].trivia != cursiv::engine::Trivia::none) trivia.set(t);
  split_leads(cfg);
  shadow_tests(cfg, rivals.reads);
  const auto original = cfg.productions.size();
  check_rebuilding(cfg, rivals.takes, trivia);
  CHECK(cfg.productions.size() > original);
}

void a_ceiling_refines_choices_without_exceeding_it() {
  constexpr unsigned copies = 8;
  Cfg cfg = choices(copies);
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

struct Spelled : cursiv::GrammarSpec {
  struct Root : rule<create<Postfix>(lit<"go">)> {};
  using entry = Root;
};

struct SpelledFolded : cursiv::GrammarSpec {
  struct Root : rule<create<Postfix>(ikw<"go">)> {};
  using entry = Root;
};

// Whether lowering `G` with its rows padded to `rows` refuses for its actions.
template <class G>
bool refuses_too_many_actions(std::size_t rows) {
  auto image = grammar_image<View<G>>();
  cursiv::engine::LowerDesc desc = kLowerDesc<G>;
  std::vector<const cursiv::engine::RowDesc*> padded(desc.rows.begin(), desc.rows.end());
  padded.resize(std::max(rows, padded.size()), padded.front());
  desc.rows = padded;
  try {
    (void)lower(image, desc);
    return false;
  } catch (const Refused& error) {
    CHECK(std::string_view(error.what()).find("too many actions") != std::string_view::npos);
    return true;
  }
}

}  // namespace

int main() {
  a_shared_shift_requires_one_trivia_setting();
  const auto small = prefix_lexer_size(64), medium = prefix_lexer_size(128);
  // Doubling the prefixes adds twice the code, allowing for wider state numbers.
  CHECK(prefix_lexer_size(256) <= medium + 5 * (medium - small) / 2);
  long_unit_chains_share_blocks_and_read_in_order();
  const auto chains = plain_tables<UnitChains>();
  CHECK(chains.hops.size() == 7);  // Empty chain, two outer rules, four base alternatives.
  CHECK(chains.corners.empty());   // No kept state completes a link of the chain.
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
  a_chain_of_terminal_calls_is_followed_once();
  shared_words_are_listed_once();
  using N = NamedClosers;
  // A closer behind a doc or a terminal's name is read as its literal is.
  CHECK(closing_table<N::lit<"*/">.doc<"Ends the comment.">()>() ==
        closing_table<N::lit<"*/">>());
  CHECK(closing_table<N::ref<N::Close>>() == closing_table<N::lit<"*/">>());
  CHECK(closing_table<N::ref<N::Again>>() == closing_table<N::lit<"*/">>());
  CHECK(modifier_counts<SharedGroup::Once>() == modifier_counts<SharedGroup::Twice>());
  rejects_second_write<UnorderedWrites>("'marked' twice");
  rejects_second_write<AliasedOperands>("'child' twice");
  rejects_second_write<SectionWrites>("'marked' twice");
  a_chain_of_rules_shares_its_inherited_writes();
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
  const auto contextSmall = implied_context_size(64);
  const auto contextMedium = implied_context_size(128);
  const auto contextLarge = implied_context_size(256);
  CHECK(contextMedium.first <= 2 * contextSmall.first);
  CHECK(contextMedium.second <= 2 * contextSmall.second);
  CHECK(contextLarge.first <= 2 * contextMedium.first);
  CHECK(contextLarge.second <= 2 * contextMedium.second);
  for (const auto n : {64u, 128u, 256u, 512u}) (void)implied_context_size(n, true);
  rebuilding_implied_contexts_preserves_infix_decisions();
  rebuilding_preserves_opening_slots();
  a_ceiling_refines_choices_without_exceeding_it();
  a_shared_reader_needs_a_terminal_that_separates_its_arms();
  CHECK(a_cycle_of_rules_is_refused<Cycles::SilentList>());
  CHECK(a_cycle_of_rules_is_refused<Cycles::NodeList>());
  CHECK(!a_cycle_of_rules_is_refused<Cycles::SolidList>());
  CHECK(!a_cycle_of_rules_is_refused<Cycles::NodeOnce>());
  CHECK(a_cycle_of_rules_is_refused<Cycles::Unit>());
  CHECK(refuses_too_many_actions<Spelled>(kActionLimit + 1));
  CHECK(!refuses_too_many_actions<Spelled>(kActionLimit));
  CHECK(!refuses_too_many_actions<SpelledFolded>(kActionLimit - 1));
  CHECK(refuses_too_many_actions<SpelledFolded>(kActionLimit));
  return cursiv::test::report("AdaptiveLr_test");
}
