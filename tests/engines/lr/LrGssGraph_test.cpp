#include "cursiv/engines/lr/runtime/LrGssGraph.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/shared/runtime/CancellationPoll.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using cursiv::LrGssGraph;
using cursiv::TextOffset;
using Id = LrGssGraph::Id;
using Reading = LrGssGraph::Reading;
using Step = LrGssGraph::Step;

Reading at(std::uint32_t state, TextOffset offset) {
  Reading r;
  r.run.state = state;
  r.at = offset;
  r.pendEnd = offset;
  return r;
}

cursiv::LrStack stack_of(std::initializer_list<std::uint16_t> states) {
  cursiv::LrStack s;
  for (const std::uint16_t state : states) s.push({state, 0, 0, 0, 0});
  return s;
}

// The first word a step's edge was laid with, carried in its unread action:
// collection renumbers steps.
std::uint32_t tag(const LrGssGraph& g, Id step) { return g.step(step).action; }

// A real token from `v` shifted onto `reading`, its edge naming `words`.
Id shift_onto(LrGssGraph& g, Id v, cursiv::SymbolId symbol, Reading reading,
              std::vector<std::uint32_t> words, bool laid = false,
              std::uint32_t action = ~0u, bool exact = false) {
  Step s;
  s.vertex = v;
  s.exact = exact;
  s.reading = g.reading(v);
  s.reading.la = {g.reading(v).at, reading.at, symbol};
  s.shifts = true;
  s.laid = laid;
  s.action = action != ~0u ? action : words.empty() ? ~0u : words.front();
  const Id step = g.add(std::move(s));
  const Id next = g.vertex(reading);
  g.edge(next, v, symbol, {}, step, words);
  return next;
}

Id shift(LrGssGraph& g, Id v, cursiv::SymbolId symbol, TextOffset to,
         std::uint32_t state, std::vector<std::uint32_t> words,
         bool laid = false) {
  return shift_onto(g, v, symbol, at(state, to), std::move(words), laid);
}

Id open_next(LrGssGraph& g) {
  Id v = LrGssGraph::none;
  TextOffset p = 0;
  CHECK(g.next_vertex(v, p));
  g.take_vertex();
  return v;
}

// Two stacks of one future, their states and symbols alike, one shifted on
// each arm and the arms met again on the next: whether the text comes back,
// and the action of the step the graph elects first.
struct Arms {
  std::vector<std::uint32_t> words;
  std::uint32_t action = ~0u;
};
struct Elected {
  bool handed = false;
  std::uint32_t first = ~0u;
  unsigned tabled = 0;
};
Elected elect(const Arms& a, const Arms& b, bool exact = false) {
  LrGssGraph g;
  g.seed(stack_of({0, 3}), at(3, 0));
  const Id s = open_next(g);
  Reading one = at(5, 1), other = at(5, 1);
  one.laMode = 1;
  other.laMode = 2;
  const Id x = shift_onto(g, s, 7, one, a.words, false, a.action, exact);
  const Id y = shift_onto(g, s, 7, other, b.words, false, b.action, exact);
  CHECK(x != y);
  CHECK(open_next(g) == x);
  const Id top = shift(g, x, 9, 2, 6, {});
  CHECK(open_next(g) == y);
  CHECK(shift(g, y, 9, 2, 6, {}) == top);
  Id v = LrGssGraph::none;
  TextOffset p = 0;
  CHECK(g.next_vertex(v, p) && v == top && p == 2);
  g.settle(p);
  Elected out;
  out.handed = g.hand_back(v, 1);
  if (out.handed) {
    const auto steps = g.elected();
    CHECK(steps.size() == 2);
    if (!steps.empty()) out.first = tag(g, steps[0]);
  }
  out.tabled = g.tabled();
  return out;
}

// The one that ranks first is the one the text comes back with.
void two_stacks_of_one_future_keep_the_one_ranked_first() {
  const Elected e = elect({{1}}, {{0}});
  CHECK(e.handed && e.first == 0 && e.tabled == 0);
}

// The words of two derivations tell them apart before the order the table lays
// its arms in is asked: the arm laid first, with the later words, loses.
void words_outweigh_the_order_of_the_table() {
  const Elected e = elect({{1}, 0}, {{0}, 9});
  CHECK(e.handed && e.first == 9 && e.tabled == 0);
}

// Where their words say nothing and the table proves its order the election's
// for every text, the arm the table lays first stays, and the graph counts the
// election the table decided.
void the_order_of_the_table_decides_what_words_leave_open() {
  const Elected e = elect({{1}, 4}, {{1}, 2}, true);
  CHECK(e.handed && e.first == 2 && e.tabled == 1);
}

// Where the table proves nothing of its order, it elects nothing: both stacks
// stay, whatever arm it lays first.
void an_order_the_table_does_not_prove_drops_no_stack() {
  const Elected e = elect({{1}, 4}, {{1}, 2});
  CHECK(!e.handed && e.tabled == 0);
}

// Two arms onto one state by two symbols: a pass-through the table skipped
// may stand under either, which their words do not say, so both stay.
void two_symbols_onto_one_state_both_stay() {
  LrGssGraph g;
  g.seed(stack_of({0, 3}), at(3, 0));
  const Id s = open_next(g);
  const Id x = shift(g, s, 7, 1, 5, {1});
  CHECK(shift(g, s, 8, 1, 5, {0}) == x);
  Id v = LrGssGraph::none;
  TextOffset p = 0;
  CHECK(g.next_vertex(v, p) && v == x && p == 1);
  g.settle(p);
  CHECK(!g.hand_back(v, 0));
}

// Two stacks through different states are two futures: neither is dropped,
// and the text is not handed back over two paths.
void two_arms_over_different_states_both_stay() {
  LrGssGraph g;
  g.seed(stack_of({0, 3}), at(3, 0));
  const Id s = open_next(g);
  shift(g, s, 7, 1, 4, {0});
  shift(g, s, 8, 1, 6, {1});
  const Id a = open_next(g);
  const Id top = shift(g, a, 9, 2, 9, {});
  const Id b = open_next(g);
  CHECK(shift(g, b, 9, 2, 9, {}) == top);
  Id v = LrGssGraph::none;
  TextOffset p = 0;
  CHECK(g.next_vertex(v, p) && v == top);
  g.settle(p);
  CHECK(!g.hand_back(v, 1));
  CHECK(!g.close());
}

// A dead arm is a frozen reading, kept until a rival reads three real tokens
// past it: the text comes back on the third.
void a_dead_arm_holds_the_text_three_tokens() {
  LrGssGraph g;
  g.seed(stack_of({0, 3}), at(3, 0));
  Id v = open_next(g);
  g.die(0);
  for (TextOffset to = 1; to <= 3; ++to) {
    v = shift(g, v, 7, to, 5, {});
    Id n = LrGssGraph::none;
    TextOffset p = 0;
    CHECK(g.next_vertex(n, p) && n == v && p == to);
    g.settle(p);
    CHECK(g.hand_back(n, p - 1) == (to == 3));
    if (to != 3) g.take_vertex();
  }
  CHECK(g.elected().size() == 3);
}

// One token past a dead arm, the vertex alone is held. The readings past it
// fork, one of them dies, and the first vertex opened two real tokens on gives
// the text back at the held vertex: the derivation ends there.
void a_reading_past_a_held_vertex_gives_the_text_back_at_it() {
  LrGssGraph g;
  g.seed(stack_of({0, 3}), at(3, 0));
  const Id s = open_next(g);
  g.die(0);
  const Id held = shift(g, s, 7, 1, 5, {});
  // As an episode reads: the nearest vertex, settled, offered, then opened.
  TextOffset openedAt = 0;
  const auto offer = [&](Id want) {
    Id v = LrGssGraph::none;
    TextOffset p = 0;
    CHECK(g.next_vertex(v, p) && v == want);
    g.settle(p);
    const bool back = g.hand_back(v, openedAt);
    if (!back) {
      g.take_vertex();
      openedAt = std::max(openedAt, p);
    }
    return back;
  };
  CHECK(!offer(held));
  const Id x = shift(g, held, 8, 2, 6, {});
  const Id y = shift(g, held, 9, 2, 7, {});
  CHECK(!offer(x));
  const Id top = shift(g, x, 7, 3, 8, {});
  CHECK(!offer(y));
  g.die(2);
  CHECK(offer(top));
  CHECK(g.elected().size() == 1);
}

// A laid indent spans real bytes but validates no reading. Both hand-back and
// the frozen sources wait for three real tokens.
void laid_shifts_do_not_validate_or_expire_frozen_readings() {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  Id v = open_next(g);
  Reading frozen = g.reading(v);
  frozen.la = {0, 0, 0};
  g.freeze(v, frozen);
  for (TextOffset to = 1; to <= 5; ++to) {
    shift(g, v, 7, to, 5, {}, to == 2 || to == 4);
    g.settle(to);
    TextOffset p = 0;
    CHECK(g.next_vertex(v, p) && p == to);
    CHECK(g.sources(1).empty() == (to == 5));
    CHECK(g.hand_back(v, to - 1) == (to == 5));
    if (to != 5) g.take_vertex();
  }
  CHECK(g.elected().size() == 5);
}

// The best stack is published last, beyond the repair's real 512-stack cap.
// The cap selects by rank, including at zero and one, rather than by DFS order.
void frozen_stacks_are_ranked_before_the_cap() {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  const Id root = open_next(g);
  std::vector<Id> arms;
  for (unsigned i = 0; i != 513; ++i)
    arms.push_back(shift(g, root, 7, 1, i + 1,
                         {i == 512 ? 0u : i + 1}));
  Id top = LrGssGraph::none;
  for (const Id arm : arms) top = shift(g, arm, 8, 2, 600, {});
  Reading frozen = g.reading(top);
  frozen.la = {2, 2, 0};
  g.freeze(top, frozen);
  CHECK(!g.close());
  CHECK(g.sources(0).empty());
  for (const unsigned cap : {1u, 2u, 512u, 513u}) {
    const auto sources = g.sources(cap);
    CHECK(sources.size() == cap);
    for (unsigned i = 0; i != sources.size(); ++i) {
      const auto steps = g.derivation(sources[i].forests);
      CHECK(steps.size() == 2);
      CHECK(tag(g, steps[0]) == i);
    }
  }
}

// A packed stack holds exponentially many paths. The first two are selected
// without enumerating all of them; equal ranks retain the first DFS paths.
void frozen_stacks_share_prefixes_and_keep_equal_rank_order() {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  Id top = open_next(g);
  for (unsigned i = 0; i != 24; ++i) {
    const Id a = shift(g, top, 7, 2 * i + 1, 2 * i + 1, {});
    const Id b = shift(g, top, 8, 2 * i + 1, 2 * i + 2, {});
    top = shift(g, a, 9, 2 * i + 2, 100, {});
    CHECK(shift(g, b, 9, 2 * i + 2, 100, {}) == top);
  }
  Reading frozen = g.reading(top);
  frozen.la = {48, 48, 0};
  g.freeze(top, frozen);
  // The graph is closed for ranking, but the differing stacks remain available.
  const auto sources = g.sources(2);
  CHECK(sources.size() == 2);
  for (const auto& s : sources) {
    CHECK(s.vertices.size() == 49 && s.forests.size() == 48);
    CHECK(g.derivation(s.forests).size() == 48);
  }
  CHECK(sources[0].vertices[1] != sources[1].vertices[1]);
  CHECK(std::equal(sources[0].vertices.begin() + 2, sources[0].vertices.end(),
                   sources[1].vertices.begin() + 2));
}

// Compare bounded selection with exhaustive election over a small packed DAG.
// Decisions at several depths make the best path differ from the first DFS
// path throughout the stack, rather than only at its top.
void bounded_frozen_stacks_match_exhaustive_election() {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  Id top = open_next(g);
  for (unsigned level = 0; level != 6; ++level) {
    std::vector<Id> arms;
    for (unsigned arm = 0; arm != 3; ++arm)
      arms.push_back(shift(g, top, 7, 2 * level + 1,
                           3 * level + arm + 1, {(arm + level + 1) % 3}));
    for (const Id arm : arms) top = shift(g, arm, 8, 2 * level + 2, 100, {});
  }
  Reading frozen = g.reading(top);
  frozen.la = {12, 12, 0};
  g.freeze(top, frozen);
  const auto all = g.sources(729);
  CHECK(all.size() == 729);
  for (const unsigned cap : {1u, 5u, 64u, 512u}) {
    const auto limited = g.sources(cap);
    CHECK(limited.size() == cap);
    for (unsigned i = 0; i != limited.size(); ++i) {
      CHECK(limited[i].vertices == all[i].vertices);
      CHECK(limited[i].forests == all[i].forests);
    }
  }
}

// Repeated reductions keep their nested forest: the rank and the step order
// of the elected derivation hold over a long run.
void repeated_reductions_keep_their_nested_rank() {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  Id top = open_next(g);
  std::vector<Id> children;
  for (TextOffset to = 1; to <= 128; ++to) {
    const Id next = g.vertex(at(5, to));
    for (const unsigned choice : {1u, 0u}) {
      Step s;
      s.vertex = top;
      s.reading = g.reading(top);
      s.reading.la = {to - 1, to, 7};
      s.shifts = true;
      s.action = choice;
      const std::array<std::uint32_t, 1> word{choice};
      g.edge(next, 0, 7, children, g.add(s), word);
    }
    g.settle(to);
    top = open_next(g);
    g.watch({top, LrGssGraph::none, LrGssGraph::none, LrGssGraph::none,
             cursiv::kLrNoProd, 1});
    LrGssGraph::Pop pop{};
    Id edge = LrGssGraph::none;
    CHECK(g.next_path(pop, edge));
    const auto path = g.children(pop, edge);
    children.assign(path.begin(), path.end());
    CHECK(!g.next_path(pop, edge));
  }
  g.accept_at(top, LrGssGraph::none);
  CHECK(g.close());
  const auto steps = g.elected();
  CHECK(steps.size() == 128);
  for (unsigned i = 0; i != steps.size(); ++i) {
    CHECK(tag(g, steps[i]) == 0);
    CHECK(g.step(steps[i]).reading.la.end == i + 1);
  }
}

// Ask for a stop from the replay itself: a large elected forest must unwind
// before calling every remaining step, without a thread or a timing race.
// A stop is read within one run of polls, and never without a token.
void a_stop_is_read_within_one_run_of_polls() {
  const cursiv::CancellationPoll none;
  for (unsigned i = 0; i < 100000; ++i) none.poll();
  none.check();
  cursiv::CancellationTokenSource source;
  const auto token = source.get_token();
  const cursiv::CancellationPoll poll(token);
  for (unsigned i = 0; i < 1000; ++i) poll.poll();
  source.request_stop();
  unsigned polled = 0;
  bool cancelled = false;
  try {
    for (; polled < 1000; ++polled) poll.poll();
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled && polled < 256);
  cancelled = false;
  try {
    poll.check();
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
}

// A real token the lexer ranked among rivals: its rank is the only word of
// its edge.
Id ranked_shift(LrGssGraph& g, Id v, cursiv::SymbolId symbol, TextOffset to,
                std::uint32_t state, std::uint32_t rank) {
  Step s;
  s.vertex = v;
  s.reading = g.reading(v);
  s.reading.la = {g.reading(v).at, to, symbol};
  s.shifts = true;
  s.ranked = true;
  s.lexRank = rank;
  s.action = rank;
  const Id step = g.add(std::move(s));
  const Id next = g.vertex(at(state, to));
  g.edge(next, v, symbol, {}, step, {});
  return next;
}

// The frame under `top` reduced onto `to`, `word` the arm written in front of
// it.
Id reduce_frame(LrGssGraph& g, Id top, cursiv::SymbolId symbol, Reading to,
                std::uint32_t word) {
  Step s;
  s.vertex = top;
  s.reading = g.reading(top);
  s.action = 100 + word;
  const Id step = g.add(std::move(s));
  g.watch({top, step, LrGssGraph::none, LrGssGraph::none, cursiv::kLrNoProd,
           1});
  LrGssGraph::Pop pop{};
  Id last = LrGssGraph::none;
  CHECK(g.next_path(pop, last));
  const std::array<Id, 1> kid{g.children(pop, last)[0]};
  const std::array<std::uint32_t, 1> words{word};
  const std::array<std::uint16_t, 1> before{0};
  const Id next = g.vertex(to);
  g.edge(next, g.below(last), symbol, kid, step, words, before);
  return next;
}

// Two arms onto one vertex, each a reduction of a ranked token: the arm written
// first reads the token the lexer ranks last.
Id arms_against_the_lexers_rank(LrGssGraph& g) {
  g.seed(stack_of({0}), at(0, 0));
  const Id root = open_next(g);
  const Id x = ranked_shift(g, root, 7, 1, 4, 5);
  const Id y = ranked_shift(g, root, 8, 1, 5, 2);
  CHECK(open_next(g) == x);
  const Id top = reduce_frame(g, x, 20, at(9, 1), 0);
  CHECK(open_next(g) == y);
  CHECK(reduce_frame(g, y, 21, at(9, 1), 1) == top);
  return top;
}

// The lexer's rank of a first token outweighs the written order wherever the
// graph elects between two stacks, and the same where a repair sets out from
// them.
void the_graph_elects_the_arm_whose_first_token_the_lexer_ranks_first() {
  LrGssGraph g;
  const Id top = arms_against_the_lexers_rank(g);
  g.accept_at(top, LrGssGraph::none);
  CHECK(g.close());
  const auto steps = g.elected();
  CHECK(steps.size() == 2);
  if (steps.size() == 2) CHECK(tag(g, steps[0]) == 2 && tag(g, steps[1]) == 101);
}

void frozen_stacks_come_in_the_order_the_lexer_ranks_their_first_tokens() {
  LrGssGraph g;
  const Id top = arms_against_the_lexers_rank(g);
  Reading frozen = g.reading(top);
  frozen.la = {1, 1, 0};
  g.freeze(top, frozen);
  const auto sources = g.sources(2);
  CHECK(sources.size() == 2);
  if (sources.size() != 2) return;
  const auto first = g.derivation(sources[0].forests);
  const auto second = g.derivation(sources[1].forests);
  CHECK(first.size() == 2 && second.size() == 2);
  if (first.size() == 2 && second.size() == 2)
    CHECK(tag(g, first[0]) == 2 && tag(g, second[0]) == 5);
}

// A real token whose step names the vertex that took it.
Id taken_shift(LrGssGraph& g, Id v, cursiv::SymbolId symbol, TextOffset to,
               std::uint32_t state) {
  return shift_onto(g, v, symbol, at(state, to), {}, false, 0);
}

// Two stacks of unequal depth onto one frozen vertex, parted by the rank of
// the first token of each, far below the last steps they took: the order
// reads the whole of both.
void the_lexers_rank_far_below_orders_frozen_stacks_of_unequal_depth() {
  constexpr unsigned kLong = 200;
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  const Id root = open_next(g);
  Id left = ranked_shift(g, root, 7, 1, 100, 5);
  Id right = ranked_shift(g, root, 8, 1, 300, 2);
  for (unsigned k = 1; k != kLong; ++k) {
    CHECK(open_next(g) == left);
    left = taken_shift(g, left, 9, k + 1, 100 + k);
    CHECK(open_next(g) == right);
    right = taken_shift(g, right, 9, k + 1, 300 + k);
  }
  CHECK(open_next(g) == left);
  const Id top = taken_shift(g, left, 9, kLong + 1, 900);
  CHECK(open_next(g) == right);
  const Id last = taken_shift(g, right, 9, kLong + 1, 901);
  Step owed;
  owed.vertex = last;
  owed.reading = g.reading(last);
  g.edge(top, last, 30, {}, g.add(std::move(owed)));
  Reading frozen = g.reading(top);
  frozen.la = {kLong + 1, kLong + 1, 0};
  g.freeze(top, frozen);
  const auto sources = g.sources(2);
  CHECK(sources.size() == 2);
  if (sources.size() != 2) return;
  CHECK(sources[0].forests.size() == kLong + 2);
  CHECK(sources[1].forests.size() == kLong + 1);
}

// A stack a hundred levels high with two arms at each: the best stacks differ
// from the first at the levels nearest the top, in the order written from the
// bottom.
void a_tall_stack_gives_the_best_of_its_exponentially_many() {
  constexpr unsigned kLevels = 100, kTop = 3;
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  Id top = open_next(g);
  std::vector<std::array<Id, 2>> arms;
  for (unsigned i = 0; i != kLevels; ++i) {
    const Id a = shift(g, top, 7, 2 * i + 1, 2 * i + 1, {0});
    const Id b = shift(g, top, 8, 2 * i + 1, 2 * i + 2, {1});
    arms.push_back({a, b});
    top = shift(g, a, 9, 2 * i + 2, 1000, {});
    CHECK(shift(g, b, 9, 2 * i + 2, 1000, {}) == top);
  }
  Reading frozen = g.reading(top);
  frozen.la = {2 * kLevels, 2 * kLevels, 0};
  g.freeze(top, frozen);
  const auto sources = g.sources(1u << kTop);
  CHECK(sources.size() == 1u << kTop);
  for (unsigned n = 0; n != sources.size() && n != 1u << kTop; ++n) {
    for (unsigned level = 0; level != kLevels; ++level) {
      const unsigned from_top = kLevels - 1 - level;
      const bool second = from_top < kTop && ((n >> from_top) & 1u);
      CHECK(sources[n].vertices[2 * level + 1] == arms[level][second ? 1 : 0]);
    }
  }
}

// Two frozen stacks whose frames spell one word run and the same run longer are
// not told apart by their words, nor by a fork: they still come in one order,
// whichever froze first.
std::vector<std::vector<Id>> prefix_spelling_sources(bool laterFirst) {
  LrGssGraph g;
  g.seed(stack_of({0}), at(0, 0));
  const Id top = open_next(g);
  const std::array<Id, 2> arms{shift(g, top, 7, 1, 1, {1}),
                               shift(g, top, 8, 1, 2, {1, 2})};
  for (std::size_t k = 0; k != arms.size(); ++k) {
    const Id v = arms[laterFirst ? arms.size() - 1 - k : k];
    Reading frozen = g.reading(v);
    frozen.la = {1, 1, 0};
    g.freeze(v, frozen);
  }
  std::vector<std::vector<Id>> out;
  for (const auto& s : g.sources(4)) out.push_back(s.vertices);
  return out;
}

void frozen_stacks_that_nothing_parts_come_in_one_order() {
  const auto forward = prefix_spelling_sources(false);
  CHECK(forward.size() == 2);
  CHECK(forward == prefix_spelling_sources(true));
}

}  // namespace

int main() {
  two_stacks_of_one_future_keep_the_one_ranked_first();
  words_outweigh_the_order_of_the_table();
  the_order_of_the_table_decides_what_words_leave_open();
  an_order_the_table_does_not_prove_drops_no_stack();
  two_symbols_onto_one_state_both_stay();
  two_arms_over_different_states_both_stay();
  a_dead_arm_holds_the_text_three_tokens();
  a_reading_past_a_held_vertex_gives_the_text_back_at_it();
  laid_shifts_do_not_validate_or_expire_frozen_readings();
  frozen_stacks_are_ranked_before_the_cap();
  frozen_stacks_share_prefixes_and_keep_equal_rank_order();
  bounded_frozen_stacks_match_exhaustive_election();
  repeated_reductions_keep_their_nested_rank();
  a_stop_is_read_within_one_run_of_polls();
  the_graph_elects_the_arm_whose_first_token_the_lexer_ranks_first();
  frozen_stacks_come_in_the_order_the_lexer_ranks_their_first_tokens();
  the_lexers_rank_far_below_orders_frozen_stacks_of_unequal_depth();
  a_tall_stack_gives_the_best_of_its_exponentially_many();
  frozen_stacks_that_nothing_parts_come_in_one_order();
  return cursiv::test::report("LrGssGraph_test");
}
