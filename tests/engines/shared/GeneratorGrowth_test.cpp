// What a generator's analyses cost as a grammar grows, and what they answer.
// The grammars are built at run time as descriptors; a cost is a work counter
// read at N and 2N.

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "cursiv/Check.hpp"
#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/ll/gen/LlFinalProgram.hpp"
#include "cursiv/ll/gen/LlLowered.hpp"
#include "cursiv/ll/gen/ll_build.hpp"
#include "cursiv/ll/gen/ll_emit_parser.hpp"
#include "cursiv/ll/gen/ll_emit_table.hpp"
#include "cursiv/lr/gen/LrFinalTable.hpp"
#include "cursiv/lr/gen/LrLowered.hpp"
#include "cursiv/lr/gen/lr_build.hpp"
#include "cursiv/lr/gen/lr_build_derived.hpp"
#include "cursiv/lr/gen/lr_build_passes.hpp"
#include "cursiv/lr/gen/lr_completion_sites.hpp"
#include "cursiv/lr/gen/lr_emit_parser.hpp"
#include "cursiv/lr/gen/lr_emit_table.hpp"
#include "cursiv/lr/gen/lr_term_run.hpp"
#include "cursiv/shared/gen/AstRegistryBuilder.hpp"
#include "cursiv/shared/gen/KeepFirst.hpp"
#include "cursiv/shared/gen/LevelFault.hpp"
#include "cursiv/shared/gen/LexModes.hpp"
#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/LexOvertakes.hpp"
#include "cursiv/shared/gen/LowerDesc.hpp"
#include "cursiv/shared/gen/gen_report.hpp"
#include "cursiv/shared/gen/optional_shape.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace {

using namespace cursiv;
using namespace cursiv::engine;
using cursiv::test::failures;

struct RuntimeGrammar {
  std::deque<cursiv::detail::ImageDesc> nodes;
  std::deque<std::vector<const cursiv::detail::ImageDesc*>> children;
  std::deque<std::string> texts;
  std::deque<cursiv::detail::ImageRuleDesc> rules;
  std::deque<cursiv::detail::ImageFaceDesc> faces;
  std::deque<cursiv::detail::ImageStageDesc> stages;
  std::deque<std::vector<cursiv::detail::ImageLevelDesc>> levels;
  std::deque<AstRowDesc> rows;
  std::deque<AstTypeDesc> types;
  std::deque<AstFieldDesc> fields;
  std::vector<const AstRowDesc*> rowList;
  std::vector<const cursiv::detail::ImageRuleDesc*> terms, nts, ignored;
  const cursiv::detail::ImageRuleDesc* entry = nullptr;
  const AstRowDesc* conversion = nullptr;
  // The word class keywords end on.
  std::string_view wordClass = "abcdefghijklmnopqrstuvwxyz0123456789_";

  RuntimeGrammar() { ignored.push_back(terminal("WS", some(set(" \t\r\n")))); }

  std::string_view text(std::string s) {
    return texts.emplace_back(std::move(s));
  }
  const cursiv::detail::ImageDesc* put(cursiv::detail::ImageDesc d) {
    return &nodes.emplace_back(d);
  }
  const cursiv::detail::ImageDesc* word(std::string s) {
    return put({.kind = cursiv::detail::ImageKind::lit,
                .flags = cursiv::detail::kLitGuardWord,
                .text = text(std::move(s))});
  }
  const cursiv::detail::ImageDesc* token(std::string s) {
    return put(
        {.kind = cursiv::detail::ImageKind::lit, .text = text(std::move(s))});
  }
  const cursiv::detail::ImageDesc* set(std::string s) {
    return put(
        {.kind = cursiv::detail::ImageKind::cpset, .text = text(std::move(s))});
  }
  const cursiv::detail::ImageDesc* group(
      cursiv::detail::ImageKind k,
      std::vector<const cursiv::detail::ImageDesc*> v) {
    const auto& at = children.emplace_back(std::move(v));
    return put({.kind = k,
                .count = static_cast<std::uint32_t>(at.size()),
                .children = at.data()});
  }
  const cursiv::detail::ImageDesc* seq(
      std::vector<const cursiv::detail::ImageDesc*> v) {
    return group(cursiv::detail::ImageKind::seq, std::move(v));
  }
  const cursiv::detail::ImageDesc* alt(
      std::vector<const cursiv::detail::ImageDesc*> v) {
    return group(cursiv::detail::ImageKind::choice, std::move(v));
  }
  const cursiv::detail::ImageDesc* repeat(std::uint64_t m, std::uint64_t n,
                                          const cursiv::detail::ImageDesc* k) {
    const auto& at = children.emplace_back(std::vector{k});
    return put({.kind = cursiv::detail::ImageKind::repeat,
                .count = 1,
                .children = at.data(),
                .m = m,
                .n = n});
  }
  const cursiv::detail::ImageDesc* opt(const cursiv::detail::ImageDesc* k) {
    return repeat(0, 1, k);
  }
  const cursiv::detail::ImageDesc* many(const cursiv::detail::ImageDesc* k) {
    return repeat(0, GrammarSpec::kUnbounded, k);
  }
  const cursiv::detail::ImageDesc* some(const cursiv::detail::ImageDesc* k) {
    return repeat(1, GrammarSpec::kUnbounded, k);
  }
  const cursiv::detail::ImageDesc* call(
      const cursiv::detail::ImageRuleDesc* r) {
    return put({.kind = cursiv::detail::ImageKind::ref, .rule = r});
  }

  // `k`, written to a member of its own.
  const cursiv::detail::ImageDesc* member(std::string name,
                                          const cursiv::detail::ImageDesc* k) {
    AstFieldDesc& f = fields.emplace_back();
    f.name = text(std::move(name));
    f.offset = +[]() -> std::ptrdiff_t { return 0; };
    f.owner = &types.emplace_back();
    AstRowDesc& row = rows.emplace_back();
    row.gen.kind = WriteKind::assign;
    row.gen.feature = f.name;
    row.field = &f;
    rowList.push_back(&row);
    const auto& at = children.emplace_back(std::vector{k});
    return put({.kind = cursiv::detail::ImageKind::assign,
                .count = 1,
                .children = at.data(),
                .row = &row});
  }

  cursiv::detail::ImageRuleDesc* declare(std::string name,
                                         cursiv::detail::ImageRuleKind kind) {
    cursiv::detail::ImageFaceDesc& f = faces.emplace_back();
    f.kind = kind;
    if (kind != cursiv::detail::ImageRuleKind::terminal)
      f.returns = text(name + "T");
    cursiv::detail::ImageStageDesc& s = stages.emplace_back();
    s.kind = kind;
    if (conversion == nullptr) {
      AstRowDesc& row = rows.emplace_back();
      row.gen.kind = WriteKind::convert;
      rowList.push_back(&row);
      conversion = &row;
    }
    s.convertRow = conversion;
    cursiv::detail::ImageRuleDesc& r = rules.emplace_back();
    r.name = text(name);
    r.qualified = text("G::" + name);
    r.stage = &s;
    r.written = &f;
    r.resolved = &f;
    (kind == cursiv::detail::ImageRuleKind::terminal ? terms : nts)
        .push_back(&r);
    return &r;
  }
  void body(cursiv::detail::ImageRuleDesc* r,
            const cursiv::detail::ImageDesc* b) {
    const_cast<cursiv::detail::ImageFaceDesc*>(r->written)->body = b;
    const_cast<cursiv::detail::ImageStageDesc*>(r->stage)->body = b;
  }
  cursiv::detail::ImageRuleDesc* terminal(std::string name,
                                          const cursiv::detail::ImageDesc* b) {
    cursiv::detail::ImageRuleDesc* r =
        declare(std::move(name), cursiv::detail::ImageRuleKind::terminal);
    body(r, b);
    return r;
  }
  cursiv::detail::ImageRuleDesc* data(std::string name) {
    return declare(std::move(name), cursiv::detail::ImageRuleKind::data);
  }
  // A precedence table of one operator per level, binary levels grouped left.
  cursiv::detail::ImageRuleDesc* infix(
      std::string name, std::vector<const cursiv::detail::ImageDesc*> ops) {
    auto& at = levels.emplace_back();
    for (const auto* op : ops) at.push_back({.op = op});
    cursiv::detail::ImageRuleDesc* r =
        declare(std::move(name), cursiv::detail::ImageRuleKind::infix);
    auto* f = const_cast<cursiv::detail::ImageFaceDesc*>(r->written);
    f->levels = at.data();
    f->levelCount = static_cast<std::uint32_t>(at.size());
    return r;
  }
  const AstRowDesc* closing() {
    AstRowDesc& row = rows.emplace_back();
    row.gen.kind = WriteKind::close;
    row.decl = &types.emplace_back();
    row.built = row.named = true;
    rowList.push_back(&row);
    return &row;
  }
  // A rule with a class of its own, or the class of `shared`.
  cursiv::detail::ImageRuleDesc* node(std::string name,
                                      const AstRowDesc* shared = nullptr) {
    cursiv::detail::ImageRuleDesc* r =
        declare(std::move(name), cursiv::detail::ImageRuleKind::node);
    const_cast<cursiv::detail::ImageStageDesc*>(r->stage)->closeRow =
        shared != nullptr ? shared : closing();
    return r;
  }

  template <class Lowered, class Lower>
  Lowered lowered(Lower&& lower, std::uint32_t gate = kOptionalGate) {
    const cursiv::detail::ImageDesc* const none =
        put({.kind = cursiv::detail::ImageKind::epsilon});
    cursiv::detail::ImageGrammarDesc grammar;
    grammar.name = "Runtime";
    grammar.entry = entry;
    grammar.ignore = ignored.data();
    grammar.ignoreCount = static_cast<std::uint32_t>(ignored.size());
    grammar.bound = {0, false, wordClass};
    grammar.plain = {0, false, " \t\r\n"};
    LowerDesc desc;
    desc.terms = terms;
    desc.nts = nts;
    desc.reached = nts;
    desc.rows = rowList;
    desc.entry = entry;
    desc.bound = grammar.bound;
    desc.indentOpen = none;
    desc.indentClose = none;
    desc.optionalGate = gate;
    cursiv::detail::GrammarImage image;
    image.open(grammar);
    return lower(image, desc);
  }
};

// Letters alone: a digit would be another class for a keyword to run into.
std::string spelled(std::string_view prefix, unsigned i) {
  std::string out(prefix);
  do {
    out += static_cast<char>('a' + i % 26);
    i /= 26;
  } while (i != 0);
  return out;
}

LlBuildWork ll_work(RuntimeGrammar& g) {
  const auto low = g.lowered<LlLowered>(ll_lower);
  CHECK(!low.fault && !low.cfg.lowerFail());
  LlBuildWork work;
  const auto built = ll_build_program(low, {.work = &work});
  CHECK(!built.fault && !built.cfg.lowerFail() && !built.overflow);
  return work;
}

LrBuildWork lr_work(RuntimeGrammar& g) {
  const auto low = g.lowered<LrLowered>(lr_lower);
  CHECK(!low.fault && !low.cfg.lowerFail());
  LrBuildWork work;
  const auto built = lr_build_automaton(low, &work);
  CHECK(!built.overflow);
  return work;
}

// N rules that each hold a list of every rule, as blocks of statements do.
LlBuildWork ll_blocks(unsigned count) {
  RuntimeGrammar g;
  auto* root = g.node("Root");
  auto* statement = g.data("Statement");
  std::vector<const cursiv::detail::ImageDesc*> arms;
  for (unsigned i = 0; i < count; ++i) {
    auto* r = g.node(spelled("R", i));
    g.body(r, g.seq({g.word(spelled("kw", i)), g.token("{"),
                     g.many(g.call(statement)), g.token("}")}));
    arms.push_back(g.call(r));
  }
  g.body(statement, g.alt(arms));
  g.body(root, g.many(g.call(statement)));
  g.entry = root;
  return ll_work(g);
}

// A position lists what each of its sources can hand back: a site holds as many
// rows as the rule it calls, and a list costs its length, not its square.
void a_record_list_costs_its_length() {
  const LlBuildWork small = ll_blocks(100), large = ll_blocks(200);
  CHECK(small.rows != 0);
  CHECK(large.rows <= 5 * small.rows);
}

// Rules that hand back a node of their own and all the next one hands back, each
// closing one class, which a record closes once: what they hold grows round after
// round, and each reads all it holds again. A pair is joined once.
void a_pair_is_joined_once() {
  const auto joins = [](unsigned count) {
    RuntimeGrammar g;
    std::vector<cursiv::detail::ImageRuleDesc*> at(count);
    const AstRowDesc* const shared = g.closing();
    for (unsigned i = 0; i < count; ++i) {
      at[i] = g.data(spelled("A", i));
      const_cast<cursiv::detail::ImageStageDesc*>(at[i]->stage)->convertRow =
          shared;
    }
    for (unsigned i = 0; i < count; ++i) {
      auto* own = g.node(spelled("N", i));
      g.body(own, g.word(spelled("kw", i)));
      g.body(at[i], i + 1 < count ? g.alt({g.call(own), g.call(at[i + 1])})
                                  : g.call(own));
    }
    auto* root = g.node("Root");
    g.body(root, g.many(g.member("items", g.call(at[0]))));
    g.entry = root;
    return ll_work(g).joins;
  };
  const std::uint64_t small = joins(100);
  const std::uint64_t large = joins(200);
  CHECK(small != 0);
  CHECK(large <= 3 * small);
}

// Every list's own ids, in the order they first stand. All the ids a list can
// hold: a scan per id would read two billion of them.
void a_list_keeps_each_id_once() {
  KeepFirst keep;
  std::vector<std::uint16_t> list{5, 3, 5, 9, 3, 0, 9};
  list.resize(keep(list));
  CHECK((list == std::vector<std::uint16_t>{5, 3, 9, 0}));
  list = {3, 3, 7};
  list.resize(keep(list));
  CHECK((list == std::vector<std::uint16_t>{3, 7}));
  std::vector<std::uint16_t> every(std::size_t{1} << 16);
  std::iota(every.begin(), every.end(), std::uint16_t{0});
  const std::clock_t from = std::clock();
  CHECK(keep(every) == every.size());
  CHECK(static_cast<double>(std::clock() - from) / CLOCKS_PER_SEC < 0.1);
}

// A rule called from N places, followed by a keyword of its own at each.
RuntimeGrammar callers(unsigned count, bool nullable) {
  RuntimeGrammar g;
  auto* shared = g.data("Shared");
  const cursiv::detail::ImageDesc* const reads = g.word("x");
  g.body(shared, nullable ? g.opt(reads) : reads);
  auto* root = g.node("Root");
  std::vector<const cursiv::detail::ImageDesc*> arms;
  for (unsigned i = 0; i < count; ++i) {
    auto* r = g.node(spelled("R", i));
    g.body(r, g.seq({g.word(spelled("op", i)), g.call(shared),
                     g.word(spelled("kw", i))}));
    arms.push_back(g.call(r));
  }
  g.body(root, g.many(g.alt(arms)));
  g.entry = root;
  return g;
}

// Whether one keyword reads over another is asked of every pair a state offers
// together, and only the pairs that stand on one text are walked, with those the
// sweep's reads were paid by: their number follows the keywords, not their pairs.
void only_terminals_that_overlap_are_walked() {
  const auto walks = [](auto&& work, unsigned count, bool nullable) {
    RuntimeGrammar g = callers(count, nullable);
    return work(g).overtakes;
  };
  const auto ll = [](RuntimeGrammar& g) { return ll_work(g); };
  const auto lr = [](RuntimeGrammar& g) { return lr_work(g); };
  CHECK(walks(ll, 400, true) <= 3 * walks(ll, 200, true));
  CHECK(walks(lr, 400, false) <= 3 * walks(lr, 200, false));
}

// Terminals built by hand, with what may follow each.
struct HandLex {
  struct Cfg {
    unsigned nterms = 1;
    std::vector<TermInfo> terms{1};
    std::vector<ByteSet> sets;
  } g;
  LexNfaPool nfa;
  std::vector<int> startOf{-1};
  std::vector<DynSet> followers{1};

  int bytes(std::string_view of) {
    ByteSet set;
    for (const char c : of) set.add(static_cast<unsigned char>(c));
    g.sets.push_back(set);
    return static_cast<int>(g.sets.size()) - 1;
  }
  int spelled(std::string_view text, int out) {
    for (auto c = text.rbegin(); c != text.rend(); ++c)
      out = nfa.chars(bytes(std::string_view(&*c, 1)), out);
    return out;
  }
  int many(int set, int out) {
    const int head = nfa.split(-1, out);
    nfa.s[static_cast<std::size_t>(head)].t1 = nfa.chars(set, head);
    return head;
  }
  // A terminal whose graph `body` builds in front of where it ends.
  template <class Body>
  unsigned terminal(Body&& body) {
    const unsigned t = g.nterms++;
    g.terms.emplace_back();
    followers.emplace_back();
    startOf.push_back(body(nfa.match(static_cast<int>(t))));
    return t;
  }
  unsigned literal(std::string_view text) {
    return terminal([&](int end) { return spelled(text, end); });
  }
  // `text`, ended there or read on by a relay.
  unsigned relayed(std::string_view text) {
    return terminal([&](int end) {
      nfa.relays.emplace_back();
      return spelled(text, nfa.split(end, nfa.relay(0)));
    });
  }
};

DynSet only(unsigned t) {
  DynSet set;
  set.set(t);
  return set;
}

// A pair the sweep rules out is one a walk finds nothing for, whichever way
// round it stands and whether or not it holds a relay. A fresh reader of a pair
// has read nothing to pay for a sweep: it walks.
void a_sweep_keeps_every_pair_a_walk_finds() {
  HandLex x;
  const unsigned ab = x.literal("ab"), abc = x.literal("abc"), c = x.literal("c");
  x.followers[ab].set(c);
  const unsigned held = x.relayed("x"), xy = x.literal("xy"), y = x.literal("y");
  x.followers[held].set(y);
  const unsigned apart = x.literal("q");
  const auto walked = [&](unsigned a, unsigned b) {
    LexOvertakes fresh(x.nfa, x.g, x.startOf, x.followers);
    return fresh.reads_over(a, only(b));
  };
  CHECK(walked(abc, ab));
  CHECK(walked(xy, held));
  CHECK(!walked(c, ab));
  CHECK(!walked(apart, ab));
  LexOvertakes swept(x.nfa, x.g, x.startOf, x.followers);
  swept.finish_sweep();
  for (unsigned a = 1; a < x.g.nterms; ++a)
    for (unsigned b = 1; b < x.g.nterms; ++b)
      CHECK(swept.reads_over(a, only(b)) == walked(a, b));
}

// Terminals that each count a letter of their own: a pair stands in a few
// subsets, and all of them together in two to the number of terminals. The
// sweep reads no more than the walks have.
void a_sweep_costs_what_the_walks_have_read() {
  HandLex x;
  constexpr std::string_view letters = "abcdefghijklmnop";
  const unsigned stop = x.literal("!");
  for (std::size_t i = 0; i < letters.size(); ++i)
    x.terminal([&](int end) {
      std::string others(letters);
      others.erase(i, 1);
      const int rest = x.bytes(others), own = x.bytes(letters.substr(i, 1));
      const int tail = x.many(rest, x.spelled("!", end));
      const int head = x.nfa.split(-1, tail);
      const int second = x.many(rest, x.nfa.chars(own, head));
      x.nfa.s[static_cast<std::size_t>(head)].t1 =
          x.many(rest, x.nfa.chars(own, second));
      return head;
    });
  for (unsigned t = 1; t < x.g.nterms; ++t) x.followers[t].set(stop);
  LexOvertakes readers(x.nfa, x.g, x.startOf, x.followers);
  for (unsigned a = 1; a < x.g.nterms; ++a)
    for (unsigned b = 1; b < x.g.nterms; ++b)
      readers.reads_over(a, only(b));
  CHECK(readers.walk_reads() != 0);
  CHECK(readers.sweep_reads() <= 2 * readers.walk_reads());
}

// The modes that cover a mode are asked of those that lex its rarest terminal: a
// mode among them, and a mode that lexes nothing, which every mode covers.
void a_mode_covers_what_it_lexes() {
  RuntimeGrammar g;
  auto* root = g.node("Root");
  g.body(root, g.word("x"));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  LexModes modes(low.cfg);
  const auto lexing = [](std::initializer_list<unsigned> terms) {
    DynSet set;
    for (const unsigned t : terms) set.set(t);
    return set;
  };
  modes.set = {lexing({1, 2}), lexing({1, 2, 3}), lexing({3}), lexing({}),
               lexing({1, 2})};
  modes.longer.assign(modes.set.size(), DynSet{});
  std::vector<std::pair<std::size_t, std::size_t>> marked, covering;
  modes.cover(false, [&](std::size_t a, std::size_t b) {
    marked.emplace_back(a, b);
  });
  for (std::size_t a = 0; a < modes.set.size(); ++a)
    for (std::size_t b = 0; b < modes.set.size(); ++b)
      if (modes.covers(a, b)) covering.emplace_back(a, b);
  std::ranges::sort(marked);
  CHECK(marked == covering);
}

// A state that offers every keyword lists each of them once.
std::uint64_t lr_expects(unsigned count) {
  RuntimeGrammar g;
  auto* root = g.node("Root");
  std::vector<const cursiv::detail::ImageDesc*> arms;
  for (unsigned i = 0; i < count; ++i) arms.push_back(g.word(spelled("kw", i)));
  g.body(root, g.many(g.seq({g.alt(arms), g.token(";")})));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  LrBuildWork work;
  LrBuildTable built = lr_build_automaton(low, &work);
  const std::vector<ElementId> sites(
      std::size_t{built.writes.nchains} + built.nfused, kNoElement);
  lr_derive_tables(built, sites);
  return work.expects;
}

void a_state_lists_its_continuations_in_their_number() {
  const std::uint64_t small = lr_expects(200);
  const std::uint64_t large = lr_expects(400);
  CHECK(small != 0);
  CHECK(large <= 3 * small);
}

void a_set_walks_its_members_in_order() {
  DynSet set;
  for (const unsigned t : set.members()) CHECK(t == ~0u);
  const std::vector<unsigned> held{0, 3, 63, 64, 200, 511};
  for (const unsigned t : held) set.set(t);
  std::vector<unsigned> seen;
  for (const unsigned t : set.members()) seen.push_back(t);
  CHECK(seen == held);
  set.reset(511);
  set.reset(200);
  seen.clear();
  for (const unsigned t : set.members()) seen.push_back(t);
  CHECK((seen == std::vector<unsigned>{0, 3, 63, 64}));
}

// A hand-written chain of precedence levels: each level's FOLLOW reaches the
// next, and a closing parenthesis comes back to the top.
RuntimeGrammar precedence_levels(unsigned levels) {
  RuntimeGrammar g;
  const cursiv::detail::ImageDesc* const name =
      g.call(g.terminal("Name", g.some(g.set("abcdefghijklmnopqrstuvwxyz"))));
  std::vector<cursiv::detail::ImageRuleDesc*> level(levels);
  for (unsigned i = 0; i < levels; ++i) level[i] = g.data(spelled("E", i));
  for (unsigned i = 0; i + 1 < levels; ++i)
    g.body(level[i],
           g.seq({g.call(level[i + 1]),
                  g.many(g.seq({g.word(spelled("op", i)),
                                g.call(level[i + 1])}))}));
  g.body(level[levels - 1],
         g.alt({name, g.seq({g.token("("), g.call(level[0]), g.token(")")})}));
  auto* root = g.node("Root");
  g.body(root, g.many(g.seq({g.call(level[0]), g.token(";")})));
  g.entry = root;
  return g;
}

// Each edge of the lookaheads' relations is read once: the chain's edges grow
// with its square, and the unions over them no faster.
void lookaheads_read_each_edge_once() {
  const auto work = [](unsigned levels) {
    RuntimeGrammar g = precedence_levels(levels);
    return lr_work(g).lookaheads;
  };
  const std::uint64_t small = work(100);
  const std::uint64_t large = work(200);
  CHECK(small != 0);
  CHECK(large <= 5 * small);
}

// Rules that each read a keyword and call the next, the last back to the first:
// what follows one follows them all, one component of the relation. It takes
// what it depends on once, however many of its transitions lead there.
void a_component_reads_each_of_its_edges_once() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g;
    std::vector<cursiv::detail::ImageRuleDesc*> at(count);
    for (unsigned i = 0; i < count; ++i) at[i] = g.data(spelled("A", i));
    for (unsigned i = 0; i + 1 < count; ++i)
      g.body(at[i], g.seq({g.word(spelled("kw", i)), g.call(at[i + 1])}));
    g.body(at[count - 1],
           g.alt({g.seq({g.word("back"), g.call(at[0])}), g.word("end")}));
    auto* root = g.node("Root");
    g.body(root, g.seq({g.call(at[0]), g.word("done")}));
    g.entry = root;
    return lr_work(g).lookaheads;
  };
  const std::uint64_t small = work(100);
  const std::uint64_t large = work(200);
  CHECK(small != 0);
  CHECK(large <= 3 * small);
}

// A rule that reads a name, then one of N operators and itself: a reduction looks
// back to N transitions, and each takes in N others. What a state flags reads what
// each transition answers, not all those it takes in.
void a_reduction_reads_what_its_transitions_answer() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g;
    const cursiv::detail::ImageDesc* const name =
        g.call(g.terminal("Name", g.some(g.set("abcdefghijklmnopqrstuvwxyz"))));
    auto* expr = g.data("Expr");
    std::vector<const cursiv::detail::ImageDesc*> arms{name};
    for (unsigned i = 0; i < count; ++i)
      arms.push_back(g.seq({name, g.token(spelled("+", i)), g.call(expr)}));
    g.body(expr, g.alt(arms));
    auto* root = g.node("Root");
    g.body(root, g.seq({g.call(expr), g.token(";")}));
    g.entry = root;
    return lr_work(g).lookaheads;
  };
  const std::uint64_t small = work(40);
  const std::uint64_t large = work(80);
  CHECK(small != 0);
  CHECK(large <= 5 * small);
}

// Two levels of N operators read by a repair: a reading walks what a list of
// states holds and returns to once, not once per state.
void a_repair_walks_each_list_of_states_once() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g;
    const cursiv::detail::ImageDesc* const name =
        g.call(g.terminal("Name", g.some(g.set("abcdefghijklmnopqrstuvwxyz"))));
    auto* low = g.data("Low");
    auto* high = g.data("High");
    std::vector<const cursiv::detail::ImageDesc*> lows{g.call(high)},
        highs{name};
    for (unsigned i = 0; i < count; ++i) {
      lows.push_back(g.seq({g.call(low), g.token(spelled("+", i)), g.call(high)}));
      highs.push_back(g.seq({g.call(high), g.token(spelled("*", i)), name}));
    }
    g.body(low, g.alt(lows));
    g.body(high, g.alt(highs));
    auto* root = g.node("Root");
    g.body(root, g.seq({g.call(low), g.token(";")}));
    g.entry = root;
    return lr_work(g).insertions;
  };
  const std::uint64_t small = work(40);
  const std::uint64_t large = work(80);
  CHECK(small != 0);
  CHECK(large <= 5 * small);
}

// The terminals of the states a repair lists apart from their ids', in order.
std::vector<std::string_view> repair_words(const LrBuildTable& built) {
  std::vector<std::string_view> words;
  for (const std::uint16_t t : built.insertWord)
    words.push_back(built.cfg.term_text(t));
  return words;
}

// A rule that can end hands a repair's reading to where it was called: two
// rules end behind their own optional token and reach one list of returns.
void a_repair_reads_on_where_a_rule_returns() {
  RuntimeGrammar g;
  const auto ending = [&](std::string name, std::string lead, std::string token) {
    auto* r = g.data(std::move(name));
    g.body(r, g.seq({g.word(std::move(lead)), g.opt(g.word(std::move(token)))}));
    return r;
  };
  auto* either = g.data("Either");
  auto* tail = g.data("Tail");
  auto* root = g.node("Root");
  const cursiv::detail::ImageDesc* const past = g.word("q");
  g.body(tail, past);
  g.body(root, g.seq({past, g.call(either), g.call(tail)}));
  g.body(either, g.alt({g.call(ending("X", "p", "z")),
                        g.call(ending("Y", "r", "y"))}));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  CHECK((repair_words(lr_build_automaton(low)) ==
         std::vector<std::string_view>{"z", "q", "y", "q"}));
}

// A rule called from two others, which two arms call: it returns to a list of
// two states, each of which returns to a list of its own. All of them are read,
// in the order the arms are written, whichever list was built first.
void a_repair_reads_every_state_of_a_list_in_order() {
  RuntimeGrammar g;
  auto* root = g.node("Root");
  auto* first = g.data("First");
  auto* second = g.data("Second");
  g.body(root, g.alt({g.seq({g.word("a"), g.call(first), g.word("x")}),
                      g.seq({g.word("b"), g.call(second), g.word("w")})}));
  auto* shared = g.data("Shared");
  g.body(shared, g.seq({g.word("p"), g.opt(g.word("z"))}));
  g.body(first, g.call(shared));
  g.body(second, g.call(shared));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  CHECK((repair_words(lr_build_automaton(low)) ==
         std::vector<std::string_view>{"z", "x", "w"}));
}

// A rule of N optional rules, each a keyword: where each is left out, an empty
// reduce leads to the next.
void an_action_is_asked_once_whether_it_may_be_refused() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g;
    auto* root = g.node("Root");
    std::vector<const cursiv::detail::ImageDesc*> parts;
    for (unsigned i = 0; i < count; ++i)
      parts.push_back(g.opt(g.word(spelled("kw", i))));
    parts.push_back(g.word("end"));
    g.body(root, g.seq(parts));
    g.entry = root;
    return lr_work(g);
  };
  const LrBuildWork small = work(40), large = work(80);
  CHECK(small.refusals != 0);
  // As many as the table holds, which an option's states make a square of.
  CHECK(large.refusals <= 5 * small.refusals);
}

// One production per subset of an alternative's optional positions: a gate
// past what the subsets' count holds is refused before any is written.
void a_gate_past_its_bound_is_refused() {
  RuntimeGrammar g;
  auto* root = g.node("Root");
  g.body(root, g.seq({g.opt(g.word("a")), g.opt(g.word("b")), g.word("end")}));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower, kOptionalGateMost);
  CHECK(!low.fault && !low.cfg.lowerFail());
  for (const std::uint32_t gate : {kOptionalGateMost + 1, 32u, 40u}) {
    bool refused = false;
    try {
      (void)g.lowered<LrLowered>(lr_lower, gate);
    } catch (const std::invalid_argument&) {
      refused = true;
    }
    CHECK(refused);
  }
}

// Rules that each call the next: what the last hands back goes up the chain.
RuntimeGrammar alias_chain(unsigned count) {
  RuntimeGrammar g;
  std::vector<cursiv::detail::ImageRuleDesc*> alias(count);
  for (unsigned i = 0; i < count; ++i) alias[i] = g.data(spelled("A", i));
  for (unsigned i = 0; i + 1 < count; ++i) g.body(alias[i], g.call(alias[i + 1]));
  g.body(alias[count - 1], g.word("x"));
  auto* root = g.node("Root");
  g.body(root, g.many(g.call(alias[0])));
  g.entry = root;
  return g;
}

void a_production_is_read_again_only_for_what_it_reads() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g = alias_chain(count);
    return lr_work(g).folds;
  };
  // A rule of the chain hands back one row per rule below it: a square of rows
  // to read, each once.
  const std::uint64_t small = work(200);
  const std::uint64_t large = work(400);
  CHECK(small != 0);
  CHECK(large <= 5 * small);
}

// Arms that part on their first token and read the same tail fuse behind it:
// another arm adds the state of its token, whatever order the rows are read in.
void states_with_one_tail_fuse() {
  const auto states = [](unsigned count) {
    RuntimeGrammar g;
    auto* root = g.node("Root");
    std::vector<const cursiv::detail::ImageDesc*> arms;
    for (unsigned i = 0; i < count; ++i)
      arms.push_back(
          g.seq({g.word(spelled("kw", i)), g.word("then"), g.word("end")}));
    g.body(root, g.many(g.alt(arms)));
    g.entry = root;
    const auto low = g.lowered<LrLowered>(lr_lower);
    return unsigned{lr_build_automaton(low).nstates};
  };
  CHECK(states(40) == states(20) + 20);
  // A run of optional rules of one class: its rows go to one another's states
  // in both directions, and read the same whichever of them is read first.
  RuntimeGrammar g;
  const AstRowDesc* const shared = g.closing();
  auto* root = g.node("Root", shared);
  std::vector<const cursiv::detail::ImageDesc*> parts;
  for (unsigned i = 0; i < 10; ++i) {
    auto* item = g.node(spelled("A", i), shared);
    g.body(item, g.word(spelled("kw", i)));
    parts.push_back(g.opt(g.call(item)));
  }
  g.body(root, g.seq(parts));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  CHECK(lr_build_automaton(low).nstates == 64);
}

// A long run of one token: its states tell themselves apart one by one from the
// end, and each only has its one predecessor to read again.
void a_state_is_read_again_only_when_it_leads_to_one_that_moved() {
  const auto work = [](unsigned count) {
    RuntimeGrammar g;
    auto* root = g.node("Root");
    std::vector<const cursiv::detail::ImageDesc*> parts(count, g.word("x"));
    parts.push_back(g.word("end"));
    g.body(root, g.seq(parts));
    g.entry = root;
    return lr_work(g).minimizes;
  };
  const std::uint64_t small = work(1000);
  const std::uint64_t large = work(2000);
  CHECK(small != 0);
  CHECK(large <= 3 * small);
}

// A table of gotos alone, whose classes split one side at a time: the class
// goes on with its largest part, and one side built twice merges with its copy.
void a_class_goes_on_with_its_largest_part() {
  const auto grown = [](unsigned count) {
    const auto n = static_cast<std::uint16_t>(2 + 2 * count + 1);
    constexpr std::uint16_t kStart = 0, kSame = 1, kOn = 0, kFirst = 1, kAt = 2;
    const auto side = [](std::uint16_t j) {
      return static_cast<std::uint16_t>(2 + j);
    };
    const auto step = [&](std::uint16_t r) {
      return static_cast<std::uint16_t>(3 + count + r);
    };
    LrBuildTable A;
    A.nstates = n;
    A.actionBase.row(0, n);
    A.actionCount.row(0, n);
    using Edge = LrGotoRows::Edge;
    std::vector<Edge> row;
    for (std::uint16_t j = 0; j <= count; ++j)
      row.push_back({static_cast<std::uint16_t>(kAt + count + j), side(j)});
    A.gotoRows.push_row(row);
    row = {{kOn, kSame}};
    A.gotoRows.push_row(row);
    for (std::uint16_t j = 0; j <= count; ++j) {
      const std::uint16_t own = j == count ? std::uint16_t{0} : j;
      row.clear();
      for (std::uint16_t r = 0; r < count; ++r)
        row.push_back({static_cast<std::uint16_t>(kAt + r),
                       own >= r ? step(r) : kSame});
      A.gotoRows.push_row(row);
    }
    for (std::uint16_t r = 0; r < count; ++r) {
      row = {r == 0 ? Edge{kFirst, kSame}
                    : Edge{kOn, step(static_cast<std::uint16_t>(r - 1))}};
      A.gotoRows.push_row(row);
    }
    CHECK(A.goto_edge(kStart, kAt + count)->to == side(0));
    CHECK(A.goto_edge(kStart, 2 * count + kAt)->to == side(count));
    CHECK(A.goto_edge(kStart, kOn) == nullptr);
    CHECK(A.goto_edge(side(1), kAt + 1)->to == step(1));
    CHECK(A.goto_edge(side(1), kAt + count) == nullptr);
    LrBuildWork work;
    A.work = &work;
    lr_minimize_states(A);
    CHECK(A.nstates == n - 1);
    return work.moves;
  };
  const std::uint64_t small = grown(100);
  const std::uint64_t large = grown(200);
  CHECK(small != 0);
  CHECK(large <= 3 * small);
}

// What the first state needs: the terminals that start the shortest readings.
template <class Build>
std::size_t closers_of_the_first_state(RuntimeGrammar& g, Build&& check) {
  const auto low = g.lowered<LrLowered>(lr_lower);
  const LrBuildTable built = lr_build_automaton(low);
  check(built);
  std::size_t closers = 0;
  for ([[maybe_unused]] const unsigned t : built.closeRow[0].members())
    ++closers;
  return closers;
}

// A rule's first terminals reach the rules that read it, whichever comes first:
// a chain of rules numbered either way.
void a_rules_first_terminals_reach_its_readers() {
  for (const bool backward : {false, true}) {
    constexpr unsigned kCount = 12;
    RuntimeGrammar g;
    std::vector<cursiv::detail::ImageRuleDesc*> at(kCount);
    for (unsigned k = 0; k < kCount; ++k) {
      const unsigned i = backward ? kCount - 1 - k : k;
      at[i] = g.data(spelled("A", i));
    }
    for (unsigned i = 0; i + 1 < kCount; ++i)
      g.body(at[i], g.call(at[i + 1]));
    g.body(at[kCount - 1], g.alt({g.word("x"), g.word("y")}));
    auto* root = g.node("Root");
    g.body(root, g.seq({g.call(at[0]), g.word("end")}));
    g.entry = root;
    const std::size_t closers = closers_of_the_first_state(
        g, [](const LrBuildTable& built) { CHECK(built.closeMax == 2); });
    CHECK(closers == 2);
  }
}

// A rule of two readings of one cost is priced once, and what reads it and
// another rule goes on counting the other.
void a_rule_is_priced_at_its_least_cost() {
  RuntimeGrammar g;
  auto* two = g.data("Two");
  g.body(two, g.alt({g.word("x"), g.word("y")}));
  auto* one = g.data("One");
  g.body(one, g.word("z"));
  auto* both = g.data("Both");
  g.body(both, g.seq({g.call(two), g.call(one)}));
  auto* root = g.node("Root");
  g.body(root, g.seq({g.call(both), g.word("end")}));
  g.entry = root;
  const std::size_t closers = closers_of_the_first_state(
      g, [](const LrBuildTable& built) { CHECK(built.closeMax == 3); });
  CHECK(closers == 2);
}

// Each position of a production reads all its rule hands back, however much of
// another's it has read.
void each_position_reads_what_its_rule_hands_back() {
  RuntimeGrammar g;
  const auto handing = [&](std::string name) {
    auto* r = g.data(name);
    auto* x = g.node(name + "x");
    g.body(x, g.word(name + "x"));
    auto* y = g.node(name + "y");
    g.body(y, g.word(name + "y"));
    g.body(r, g.alt({g.call(x), g.call(y)}));
    return r;
  };
  auto* left = handing("l");
  auto* right = handing("r");
  auto* root = g.node("Root");
  g.body(root, g.seq({g.member("a", g.call(left)),
                      g.member("b", g.call(right))}));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  const LrBuildTable built = lr_build_automaton(low);
  // Either member closes over all that is handed to it, and over nothing.
  std::map<ElementId, unsigned> bySite;
  for (unsigned i = 0; i < built.nfused; ++i) ++bySite[built.fused[i].site];
  unsigned full = 0;
  for (const auto& [site, rows] : bySite) full += rows == 3 ? 1u : 0u;
  CHECK(full == 2);
}

// A state reached from two others, whose reductions a longer token splits: `ab`
// then `c` follows one rule, `abc` the other. With `wrapped`, the callers a
// rule ends read the sets.
bool flags_a_mode(bool answersBoth, bool wrapped = false) {
  RuntimeGrammar g;
  auto* pre = g.data("Pre");
  g.body(pre, g.word("p"));
  auto* other = g.data("Other");
  g.body(other, g.word("p"));
  auto* one = g.data("One");
  g.body(one, g.call(pre));
  auto* two = g.data("Two");
  g.body(two, g.call(pre));
  const cursiv::detail::ImageDesc* const firstReads =
      g.call(wrapped ? one : pre);
  const cursiv::detail::ImageDesc* const secondReads =
      g.call(wrapped ? two : pre);
  auto* root = g.node("Root");
  const cursiv::detail::ImageDesc* const ab = g.token("ab");
  const cursiv::detail::ImageDesc* const abc = g.token("abc");
  const cursiv::detail::ImageDesc* const c = g.token("c");
  const cursiv::detail::ImageDesc* const first = g.token("a");
  const cursiv::detail::ImageDesc* const second = g.token("b");
  const cursiv::detail::ImageDesc* const elsewhere =
      answersBoth ? g.token("z") : abc;
  g.body(root, g.alt({g.seq({first, firstReads, ab, c}),
                      answersBoth ? g.seq({second, secondReads, abc})
                                  : g.seq({second, secondReads, ab, c}),
                      g.seq({first, g.call(other), elsewhere}),
                      g.seq({second, g.call(other), elsewhere})}));
  g.entry = root;
  const auto low = g.lowered<LrLowered>(lr_lower);
  const LrBuildTable built = lr_build_automaton(low);
  bool any = false;
  for (unsigned m = 0; m < built.nmodes; ++m)
    any = any || built.flags_mode(m);
  return any;
}

void a_set_read_twice_flags_nothing() {
  CHECK(!flags_a_mode(false));
  CHECK(flags_a_mode(true));
  CHECK(!flags_a_mode(false, true));
  CHECK(flags_a_mode(true, true));
}

// Two rules that read one text before one token: the cell's order follows the
// arms of the choice that calls them, not the order the rules are numbered in.
void a_cell_forks_in_the_order_of_its_arms() {
  const auto first_rule = [](bool swapped) {
    RuntimeGrammar g;
    auto* p = g.node("P");
    g.body(p, g.word("t"));
    auto* q = g.node("Q");
    g.body(q, g.word("t"));
    auto* root = g.node("Root");
    const cursiv::detail::ImageDesc* const end = g.word("end");
    const cursiv::detail::ImageDesc* const toP = g.seq({g.call(p), end});
    const cursiv::detail::ImageDesc* const toQ = g.seq({g.call(q), end});
    g.body(root, swapped ? g.alt({toQ, toP}) : g.alt({toP, toQ}));
    g.entry = root;
    const auto low = g.lowered<LrLowered>(lr_lower);
    const LrBuildTable built = lr_build_automaton(low);
    std::uint32_t first = ~0u;
    unsigned cells = 0;
    for (unsigned s = 0; s < built.nstates; ++s) {
      const auto row = built.action_row(s);
      lr_each_run(row.data(), row.size(), [&](std::size_t i, std::size_t j) {
        if (j - i < 2) return;
        ++cells;
        first = built.cfg.prods[row[i].arg].lhs;
      });
    }
    CHECK(cells == 1);
    return first;
  };
  CHECK(first_rule(false) != first_rule(true));
}

// Rules that read a keyword of their own or the next rule, numbered either way:
// what each may begin with grows along the chain.
LlBuildWork ll_growing_firsts(unsigned count, bool backward) {
  RuntimeGrammar g;
  std::vector<cursiv::detail::ImageRuleDesc*> at(count);
  for (unsigned k = 0; k < count; ++k) {
    const unsigned i = backward ? count - 1 - k : k;
    at[i] = g.data(spelled("A", i));
  }
  for (unsigned i = 0; i + 1 < count; ++i)
    g.body(at[i], g.alt({g.word(spelled("kw", i)), g.call(at[i + 1])}));
  g.body(at[count - 1], g.word("x"));
  auto* root = g.node("Root");
  g.body(root, g.many(g.call(at[0])));
  g.entry = root;
  return ll_work(g);
}

// A rule reads what it calls once that is final, however the rules are
// numbered: its visits follow its size, not the depth of the chain below it.
void a_fact_reaches_its_readers_final() {
  for (const bool backward : {false, true}) {
    const std::uint64_t small = ll_growing_firsts(100, backward).first;
    const std::uint64_t large = ll_growing_firsts(200, backward).first;
    CHECK(small != 0);
    CHECK(large <= 3 * small);
  }
}

// Runs `body` on a thread with a 256 KB stack: true where it ran to its end.
bool on_small_stack(void (*body)()) {
#if defined(__unix__) || defined(__APPLE__)
  struct Run {
    void (*body)();
    bool ran = false;
  } run{body};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, std::size_t{1} << 18);
  pthread_t thread;
  CHECK(pthread_create(
            &thread, &attr,
            [](void* at) -> void* {
              auto* r = static_cast<Run*>(at);
              r->body();
              r->ran = true;
              return nullptr;
            },
            &run) == 0);
  pthread_join(thread, nullptr);
  pthread_attr_destroy(&attr);
  return run.ran;
#else
  body();
  return true;
#endif
}

// A terminal of many keywords: the walk that spells them keeps its pending
// branches off the native stack.
void many_keywords_spell_on_a_small_stack() {
  CHECK(on_small_stack([] {
    RuntimeGrammar g;
    std::vector<const cursiv::detail::ImageDesc*> words;
    for (unsigned i = 0; i < 3000; ++i)
      words.push_back(g.word(spelled("", i + 26)));
    auto* root = g.node("Root");
    g.body(root, g.member("v", g.call(g.terminal("KW", g.alt(words)))));
    g.entry = root;
    (void)ll_work(g);
  }));
}

// A keyword of twelve Greek letters whose cases start on different bytes has
// 4 096 spellings, more than are cut. Trivia that starts with its letters is
// refused where it reads on past the keyword by a byte or a scalar out of the
// word class, or from within a nested run, not where it reads on by word
// bytes or scalars alone.
void trivia_past_an_uncut_keyword_is_refused() {
  using cursiv::detail::ImageDesc;
  using cursiv::detail::ImageKind;
  const auto spelled = [](RuntimeGrammar& g, std::uint8_t guard) {
    return g.put({.kind = ImageKind::lit,
                  .flags = static_cast<std::uint8_t>(kLitFolded | guard),
                  .text = "ΥΠΕΡΣΥΝΤΡΙΠΤΙΚΟΤΑΤΟΣ"});
  };
  // `remark(g, r)` writes the body of trivia terminal `r`.
  const auto why = [&](const auto& remark) {
    RuntimeGrammar g;
    g.wordClass = "0-9A-Z_a-zΑ-Ωα-ω";
    auto* trivia = g.terminal("REMARK", g.token("x"));
    remark(g, trivia);
    g.ignored.push_back(trivia);
    auto* root = g.node("Root");
    g.body(root, g.member("v", spelled(g, kLitGuardWord)));
    g.entry = root;
    return ll_build_program(g.lowered<LlLowered>(ll_lower)).cfg.lowerWhy;
  };
  const auto reads_on = [&](std::uint8_t flags, std::string_view rest) {
    return why([&](RuntimeGrammar& g, auto* trivia) {
      g.body(trivia,
             g.seq({spelled(g, 0), g.many(g.put({.kind = ImageKind::cpset,
                                                 .flags = flags,
                                                 .text = rest}))}));
    });
  };
  CHECK(reads_on(0, "abc") == LowerFail::none);
  CHECK(reads_on(0, "α-ω") == LowerFail::none);
  CHECK(reads_on(kCpNot, "\n") == LowerFail::keywordSpellings);
  CHECK(reads_on(0, "α-ωΪ") == LowerFail::keywordSpellings);
  // A nested run opened by the keyword's first letters, closed by `}`.
  CHECK(why([](RuntimeGrammar& g, auto* trivia) {
          const ImageDesc* open = g.token("ΥΠΕΡ");
          const ImageDesc* close = g.token("}");
          const ImageDesc* other = g.seq(
              {g.group(ImageKind::notOf, {open}),
               g.group(ImageKind::notOf, {close}),
               g.put({.kind = ImageKind::cpset, .flags = kCpNot, .text = ""})});
          g.body(trivia,
                 g.seq({open, g.many(g.alt({g.call(trivia), other})), close}));
        }) == LowerFail::keywordSpellings);
}

// An expression nested thousands deep, in a rule, in a terminal that calls
// another halfway and in a condition: no walk of either generator, from the
// image to the emitted parser and the warnings, descends the native stack.
void deep_nesting_generates_on_a_small_stack() {
  CHECK(on_small_stack([] {
    for (const bool lr : {false, true}) {
      RuntimeGrammar g;
      const cursiv::detail::ImageDesc* e =
          g.alt({g.word("z"), g.put({.kind = cursiv::detail::ImageKind::eof})});
      for (unsigned i = 0; i < 2000; ++i) {
        const auto* inner = i % 4 == 0   ? e
                            : i % 4 == 1 ? g.opt(e)
                            : i % 4 == 2
                                ? g.many(e)
                                : g.group(cursiv::detail::ImageKind::skip, {e});
        e = g.alt({g.word(spelled("k", i)),
                   g.seq({g.token("("), inner, g.token(")")})});
      }
      const cursiv::detail::ImageDesc* t = g.token("y");
      for (unsigned i = 0; i < 2000; ++i) {
        if (i == 1000) t = g.call(g.terminal("INNER", t));
        if (i == 1500)
          t = g.seq({g.group(cursiv::detail::ImageKind::andOf, {t}), t});
        const auto* inner = i % 3 == 0 ? t : i % 3 == 1 ? g.opt(t) : g.many(t);
        t = g.alt({g.token("y"), g.seq({g.token("["), inner, g.token("]")})});
      }
      const cursiv::detail::ImageDesc* c = g.word("q");
      for (unsigned i = 0; i < 2000; ++i)
        c = g.alt({g.word("q"), g.seq({g.token("{"), i % 2 == 0 ? c : g.opt(c),
                                       g.token("}")})});
      auto* root = g.node("Root");
      auto* deep = g.terminal("DEEP", t);
      g.body(root,
             g.seq({g.member("v", e),
                    g.group(cursiv::detail::ImageKind::andOf, {g.call(deep)}),
                    g.member("w", g.call(deep)),
                    g.group(cursiv::detail::ImageKind::andOf, {c}),
                    g.member("x", c)}));
      g.entry = root;
      g.lowered<int>([&](cursiv::detail::GrammarImage& image,
                         const LowerDesc& desc) {
        CHECK(!refuse_self_bounded_arms("Deep", image, desc.reached));
        if (lr) {
          auto graph = std::make_unique<LrBuildTable>(
              lr_build_automaton(lr_lower(image, desc)));
          std::vector<const AstTypeDesc*> types;
          for (const AstTypeDesc& t : g.types) types.push_back(&t);
          const auto rows =
              build_ast_registry(g.rowList, types, {}, "Deep").rows;
          const auto sites = lr_completion_sites(
              lr_record_gens(lr_entry_table(*graph), rows, graph->writes));
          const LrFinalTable done = LrFinalTable::make(std::move(graph), sites);
          (void)lr_emit_table(done, "deep");
          (void)lr_emit_parser_files(done, "deep");
        } else {
          const auto done = LlFinalProgram::make(
              ll_build_program(ll_lower(image, desc)), "Deep");
          CHECK(done.has_value());
          if (done)
            (void)ll_emit_parser_files(*done, "deep",
                                       ll_emit_table(*done, "deep"));
        }
        report_grammar_warnings("Deep", image, desc.reached);
        return 0;
      });
    }
  }));
}

// A terminal that calls itself last, behind a prefix nested thousands deep:
// the check that it reads a regular closure keeps off the native stack.
void right_linear_terminal_generates_on_a_small_stack() {
  CHECK(on_small_stack([] {
    RuntimeGrammar g;
    const cursiv::detail::ImageDesc* u = g.token("u");
    for (unsigned i = 0; i < 4000; ++i)
      u = g.alt({g.token("u"), g.seq({g.token("<"), u, g.token(">")})});
    auto* tail = g.terminal("TAIL", u);
    g.body(tail, g.seq({u, g.opt(g.call(tail))}));
    auto* root = g.node("Root");
    g.body(root, g.member("t", g.call(tail)));
    g.entry = root;
    (void)ll_build_program(g.lowered<LlLowered>(ll_lower));
  }));
}

// A chain of rules, each opening with a call to the next: finding the
// alphabet of a record's first token keeps off the native stack.
void rule_chain_generates_on_a_small_stack() {
  CHECK(on_small_stack([] {
    RuntimeGrammar g;
    std::vector<cursiv::detail::ImageRuleDesc*> rules;
    for (unsigned i = 0; i < 4000; ++i)
      rules.push_back(g.node(spelled("R", i)));
    for (unsigned i = 0; i + 1 < rules.size(); ++i)
      g.body(rules[i],
             g.seq({g.member("a", g.call(rules[i + 1])), g.token("x")}));
    g.body(rules.back(), g.member("z", g.word("z")));
    auto* root = g.node("Root");
    g.body(root, g.member("v", g.call(rules[0])));
    g.entry = root;
    g.lowered<int>(
        [&](cursiv::detail::GrammarImage& image, const LowerDesc& desc) {
          const auto done = LlFinalProgram::make(
              ll_build_program(ll_lower(image, desc)), "Chain");
          CHECK(done.has_value());
          if (done)
            (void)ll_emit_parser_files(*done, "chain",
                                       ll_emit_table(*done, "chain"));
          return 0;
        });
  }));
}

// Data rules read in place, each a choice of the next and a token: each
// conversion goes on every token below it, found off the native stack. The
// first rule's definition reads the whole chain; the next ones run out of
// positions.
void alias_chain_lowers_on_a_small_stack() {
  CHECK(on_small_stack([] {
    RuntimeGrammar g;
    std::vector<cursiv::detail::ImageRuleDesc*> alias;
    for (unsigned i = 0; i < 2000; ++i) {
      alias.push_back(g.data(spelled("A", i)));
      const_cast<cursiv::detail::ImageStageDesc*>(alias.back()->stage)
          ->inPlace = true;
    }
    for (unsigned i = 0; i + 1 < alias.size(); ++i)
      g.body(alias[i], g.alt({g.call(alias[i + 1]), g.token(spelled("o", i))}));
    g.body(alias.back(), g.token("z"));
    auto* root = g.node("Root");
    g.body(root, g.member("v", g.call(alias[0])));
    g.entry = root;
    LowerFail why = LowerFail::none;
    try {
      (void)g.lowered<LlLowered>(ll_lower);
    } catch (const LowerRefused& refused) {
      why = refused.why;
    }
    CHECK(why == LowerFail::tooManyPositions);
  }));
}

// Two levels of a table may not share an operator; one that repeats with no
// maximum is compared by its first tokens, a pattern terminal by itself.
void a_table_compares_its_levels() {
  const auto fault = [](RuntimeGrammar& g) {
    return g.lowered<GrammarFault>(
        [&](cursiv::detail::GrammarImage& image, const LowerDesc&) {
          std::vector<std::uint32_t> rules;
          for (const auto* r : g.nts) rules.push_back(image.rule(*r));
          return level_fault(image, rules);
        });
  };
  const auto table = [&](const auto& ops) {
    RuntimeGrammar g;
    std::vector<const cursiv::detail::ImageDesc*> levels;
    for (const auto& op : ops) levels.push_back(op(g));
    g.entry = g.infix("Sum", levels);
    return fault(g);
  };
  using Op = std::function<const cursiv::detail::ImageDesc*(RuntimeGrammar&)>;
  const Op minus = [](RuntimeGrammar& g) { return g.token("-"); };
  const Op dashes = [](RuntimeGrammar& g) {
    return g.seq({g.token("-"), g.many(g.token("~"))});
  };
  const Op tildes = [](RuntimeGrammar& g) {
    return g.seq({g.token("*"), g.many(g.token("-"))});
  };
  CHECK(table(std::vector{minus, minus}).code ==
        GrammarFaultCode::precedenceClash);
  // An operator that ignores case shares the texts of its cases.
  const Op lower = [](RuntimeGrammar& g) { return g.token("and"); };
  const Op upper = [](RuntimeGrammar& g) { return g.token("AND"); };
  const Op folded = [](RuntimeGrammar& g) {
    return g.put({.kind = cursiv::detail::ImageKind::lit,
                  .flags = kLitFolded,
                  .text = "And"});
  };
  CHECK(!table(std::vector{lower, upper}));
  CHECK(table(std::vector{lower, folded}).code ==
        GrammarFaultCode::precedenceClash);
  CHECK(table(std::vector{folded, upper}).code ==
        GrammarFaultCode::precedenceClash);
  CHECK(!table(std::vector{minus, tildes}));
  const GrammarFault opens = table(std::vector{dashes, minus});
  CHECK(opens.code == GrammarFaultCode::precedenceClash && opens.gated);
  RuntimeGrammar g;
  auto* op = g.terminal("OP", g.some(g.set("+-")));
  auto* other = g.terminal("OTHER", g.some(g.set("+-")));
  g.entry = g.infix("Sum", {g.call(op), g.call(other)});
  CHECK(!fault(g));
  RuntimeGrammar same;
  auto* once = same.terminal("OP", same.some(same.set("+-")));
  same.entry = same.infix("Sum", {same.call(once), same.call(once)});
  CHECK(fault(same).code == GrammarFaultCode::precedenceClash);
}

}  // namespace

int main() {
  a_table_compares_its_levels();
  many_keywords_spell_on_a_small_stack();
  trivia_past_an_uncut_keyword_is_refused();
  deep_nesting_generates_on_a_small_stack();
  rule_chain_generates_on_a_small_stack();
  right_linear_terminal_generates_on_a_small_stack();
  alias_chain_lowers_on_a_small_stack();
  a_fact_reaches_its_readers_final();
  a_class_goes_on_with_its_largest_part();
  a_rules_first_terminals_reach_its_readers();
  a_rule_is_priced_at_its_least_cost();
  each_position_reads_what_its_rule_hands_back();
  a_set_read_twice_flags_nothing();
  a_cell_forks_in_the_order_of_its_arms();
  a_sweep_keeps_every_pair_a_walk_finds();
  a_sweep_costs_what_the_walks_have_read();
  a_mode_covers_what_it_lexes();
  a_list_keeps_each_id_once();
  a_repair_walks_each_list_of_states_once();
  a_repair_reads_on_where_a_rule_returns();
  a_repair_reads_every_state_of_a_list_in_order();
  a_reduction_reads_what_its_transitions_answer();
  a_component_reads_each_of_its_edges_once();
  a_pair_is_joined_once();
  a_state_is_read_again_only_when_it_leads_to_one_that_moved();
  states_with_one_tail_fuse();
  a_production_is_read_again_only_for_what_it_reads();
  an_action_is_asked_once_whether_it_may_be_refused();
  a_gate_past_its_bound_is_refused();
  lookaheads_read_each_edge_once();
  a_set_walks_its_members_in_order();
  a_record_list_costs_its_length();
  only_terminals_that_overlap_are_walked();
  a_state_lists_its_continuations_in_their_number();
  return cursiv::test::report("GeneratorGrowth_test");
}
