#include "cursiv/engines/shared/gen/LexDfaBuild.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LexOwedRows.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/gen/lex_boundary_misses.hpp"
#include "cursiv/engines/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/engines/shared/gen/lex_reserve.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/test/Check.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace lex_nfa_cases {

// The lexer NFA's walks against the plain ones they replace: the closure of a
// state in preorder, one step over a byte or a set of them, the refusals that
// stand at a token's head, and whether one terminal's text starts another's.

namespace {

using cursiv::ByteSet;
using cursiv::LexNfaPool;
using cursiv::LexNfaState;

// Random sets use only a..f. The other bytes share an empty transition;
// retain low/high representatives as well as every byte in that alphabet.
constexpr std::array<unsigned char, 8> probe_bytes{0, 'a', 'b', 'c',
                                                  'd', 'e', 'f', 255};

struct Rng {
  std::uint64_t s;
  unsigned next(unsigned n) {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<unsigned>((s >> 33) % n);
  }
};

void closure(const LexNfaPool& n, std::vector<int>& set, int at) {
  for (const int x : set)
    if (x == at) return;
  set.push_back(at);
  const LexNfaState& st = n.s[static_cast<std::size_t>(at)];
  if (st.set < 0) {
    if (st.t1 >= 0) closure(n, set, st.t1);
    if (st.t2 >= 0) closure(n, set, st.t2);
  }
}

std::vector<int> step(const LexNfaPool& n, const ByteSet* sets,
                      const std::vector<int>& from, unsigned char c) {
  std::vector<int> to;
  for (const int x : from) {
    const LexNfaState& s = n.s[static_cast<std::size_t>(x)];
    if (s.set >= 0 && sets[s.set].has(c) &&
        (s.minus < 0 || !sets[s.minus].has(c)))
      closure(n, to, s.t1);
  }
  std::ranges::sort(to);
  return to;
}

std::vector<bool> heads(const LexNfaPool& n, const std::vector<int>& startOf) {
  std::vector<bool> head(n.guards.size());
  for (std::size_t k = 0; k < n.guards.size(); ++k) {
    std::vector<int> seen, todo{startOf[n.guards[k].term]};
    while (!todo.empty() && !head[k]) {
      const int x = todo.back();
      todo.pop_back();
      if (x < 0 || std::ranges::find(seen, x) != seen.end()) continue;
      seen.push_back(x);
      head[k] = x == n.guards[k].hole;
      const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
      if (st.set >= 0) continue;
      todo.push_back(st.t1);
      todo.push_back(st.t2);
      for (const cursiv::LexNfaGuard& gd : n.guards)
        if (gd.hole == x) todo.push_back(gd.out);
    }
  }
  return head;
}

bool meets(const LexNfaPool& n, const ByteSet* sets, int a, int b) {
  using Pair = std::pair<std::vector<int>, std::vector<int>>;
  Pair start;
  closure(n, start.first, a);
  closure(n, start.second, b);
  std::ranges::sort(start.first);
  std::ranges::sort(start.second);
  std::vector<Pair> work{start};
  std::set<Pair> seen{start};
  for (std::size_t i = 0; i < work.size(); ++i) {
    const Pair at = work[i];
    for (const int x : at.first)
      if (n.s[static_cast<std::size_t>(x)].accept >= 0) return true;
    for (const unsigned char c : probe_bytes) {
      Pair next{step(n, sets, at.first, c), step(n, sets, at.second, c)};
      if (next.first.empty() || next.second.empty()) continue;
      if (seen.insert(next).second) work.push_back(std::move(next));
    }
  }
  return false;
}

ByteSet some_of(Rng& r) {
  ByteSet s;
  for (unsigned i = 0, k = 1 + r.next(4); i < k; ++i)
    s.add(static_cast<unsigned char>('a' + r.next(6)));
  return s;
}

void the_walks_agree_with_the_plain_ones() {
  Rng r{99};
  for (unsigned round = 0; round < 300; ++round) {
    ByteSet sets[6];
    for (ByteSet& s : sets) s = some_of(r);
    LexNfaPool n;
    const unsigned size = 8 + r.next(30);
    for (unsigned i = 0; i < size; ++i) {
      LexNfaState st;
      switch (r.next(5)) {
        case 0:
        case 1:
          st.set = static_cast<int>(r.next(6));
          st.t1 = static_cast<int>(r.next(size));
          if (r.next(4) == 0) st.minus = static_cast<int>(r.next(6));
          break;
        case 2:
          st.t1 = static_cast<int>(r.next(size));
          st.t2 = static_cast<int>(r.next(size));
          break;
        case 3:
          st.t1 = static_cast<int>(r.next(size));
          break;
        default:
          st.accept = static_cast<int>(r.next(3));
      }
      n.push(st);
    }
    std::vector<int> startOf;
    for (unsigned t = 0; t < 3; ++t)
      startOf.push_back(static_cast<int>(r.next(size)));
    for (unsigned k = 0, g = r.next(5); k < g; ++k) {
      const auto out = static_cast<int>(r.next(size));
      const auto node = static_cast<std::uint16_t>(r.next(3));
      n.term = static_cast<std::uint16_t>(r.next(3));
      n.s[r.next(size)].t2 = n.guard(node, out);
    }

    for (unsigned k = 0; k < 6; ++k) {
      std::vector<int> roots, want;
      for (unsigned i = 0, m = 1 + r.next(3); i < m; ++i)
        roots.push_back(static_cast<int>(r.next(size)));
      for (const int root : roots) closure(n, want, root);
      CHECK(n.eclose(roots) == want);
      CHECK(n.eclose(roots.front()) ==
            [&] {
              std::vector<int> one;
              closure(n, one, roots.front());
              return one;
            }());

      std::vector<int> from = n.eclose(roots);
      ByteSet bytes = some_of(r);
      std::set<int> unioned;
      for (const unsigned char b : probe_bytes) {
        const std::vector<int> plain = step(n, sets, from, b);
        CHECK(cursiv::lex_nfa_step(n, sets, from, b) == plain);
        if (bytes.has(b)) unioned.insert(plain.begin(), plain.end());
      }
      CHECK(cursiv::lex_nfa_step_any(n, sets, from, bytes) ==
            std::vector<int>(unioned.begin(), unioned.end()));
    }
    CHECK(cursiv::lex_guard_heads(n, startOf) == heads(n, startOf));
    for (unsigned k = 0; size <= 14 && k < 4; ++k) {
      const int a = static_cast<int>(r.next(size)),
                b = static_cast<int>(r.next(size));
      CHECK(cursiv::lex_prefix_meets(n, sets, a, b) ==
            (meets(n, sets, a, b) ? cursiv::LexOverlap::found
                                  : cursiv::LexOverlap::none));
    }
  }
}

// A relay that asserts resumes its continuation only where the assertion
// holds, so the pair walk enters it or not: a text read past it may meet,
// which the walk does not prove, and a tie its continuation would outrank
// still stands where it fails. Each assertion doubles the subsets a position
// stands for: past a few dozen, the walk keeps the one none holds in, unsure.
void an_assertion_is_entered_or_not() {
  ByteSet sets[2];
  sets[0].add('#');
  sets[1].add('!');
  LexNfaPool n;
  const auto asserting = [&](int out) {
    cursiv::LexRelayBuild r;
    r.row.predicate = 0;
    r.resumeOut = out;
    n.relays.push_back(r);
    return n.relay(static_cast<int>(n.relays.size()) - 1);
  };
  const int pattern = n.match(1);
  const int literal = n.match(1, true);
  // `#` behind an assertion meets the start of `#!` only where it holds.
  const int hash = n.chars(0, asserting(pattern));
  const int bang = n.chars(0, n.chars(1, n.match(2)));
  CHECK(cursiv::lex_prefix_meets(n, sets, hash, bang) ==
        cursiv::LexOverlap::incomplete);
  // `#!` at a literal's rank behind an assertion, or as a pattern, ties with
  // the pattern `#!` where the assertion fails.
  const int either = n.split(n.chars(0, n.chars(1, asserting(literal))),
                             n.chars(0, n.chars(1, pattern)));
  const auto ranked = [&](const std::vector<int>& sub) {
    return std::ranges::any_of(sub, [&](int x) {
      const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
      return st.accept >= 0 && st.lit;
    });
  };
  CHECK(cursiv::lex_pair_search(
            n, sets, either, bang,
            [&](const std::vector<int>& x, const std::vector<int>& y) {
              return cursiv::lex_accepts(n, x) && cursiv::lex_accepts(n, y) &&
                     ranked(x) == ranked(y);
            }) == cursiv::LexOverlap::found);
  // Sixteen assertions at one position.
  int many = asserting(pattern);
  for (int k = 1; k < 16; ++k)
    many = n.split(asserting(n.chars(1, pattern)), many);
  std::vector<int> at = n.eclose(many);
  std::ranges::sort(at);
  bool unsure = false;
  CHECK(cursiv::lex_assertion_worlds(n, at, unsure) ==
        std::vector<std::vector<int>>{at});
  CHECK(unsure);
}

// A nested relay's run is followed while no opener deepens it: a text it
// closes at depth zero ties with another graph's, and one an opener deepens,
// which the relay reads on past, leaves the walk incomplete.
void a_nested_run_is_followed_at_depth_zero() {
  ByteSet sets[3];
  sets[0].add('/');
  sets[1].add('*');
  sets[2].add('x');
  LexNfaPool n;
  const auto spell = [&](std::string_view text, int out) {
    for (std::size_t i = text.size(); i-- > 0;)
      out = n.chars(text[i] == '/' ? 0 : text[i] == '*' ? 1 : 2, out);
    return out;
  };
  cursiv::LexRelayBuild r;
  r.row.nested = true;
  r.open = "/*";
  r.close = "*/";
  r.resumeOut = n.match(1);
  n.relays.push_back(r);
  const int comment = spell("/*", n.relay(0));
  const auto ties = [&](int text) {
    return cursiv::lex_pair_search(
        n, sets, comment, text,
        [&](const std::vector<int>& x, const std::vector<int>& y) {
          return cursiv::lex_accepts(n, x) && cursiv::lex_accepts(n, y);
        });
  };
  using cursiv::LexOverlap;
  CHECK(ties(spell("/*x*/", n.match(2))) == LexOverlap::found);
  CHECK(ties(spell("/**/", n.match(2))) == LexOverlap::found);
  CHECK(ties(spell("/*/**/", n.match(2))) == LexOverlap::incomplete);
  CHECK(ties(spell("/*x*", n.match(2))) == LexOverlap::none);
}

// An echo is a relay the walk does not follow: a text that reads on beside it
// leaves the walk incomplete, and one that parts before it is proven apart.
void an_echo_leaves_the_walk_incomplete() {
  ByteSet sets[2];
  sets[0].add('[');
  sets[1].add('x');
  LexNfaPool n;
  cursiv::LexRelayBuild r;
  r.row.closes = true;
  r.resumeOut = n.match(1);
  n.relays.push_back(r);
  const int echo = n.chars(0, n.chars(0, n.relay(0)));
  const auto meets = [&](int text) {
    return cursiv::lex_prefix_meets(n, sets, text, echo);
  };
  using cursiv::LexOverlap;
  CHECK(meets(n.chars(0, n.chars(0, n.chars(1, n.match(2))))) ==
        LexOverlap::incomplete);
  CHECK(meets(n.chars(0, n.chars(1, n.match(2)))) == LexOverlap::none);
  CHECK(meets(n.chars(0, n.match(2))) == LexOverlap::found);
}

}  // namespace

void run() {
  the_walks_agree_with_the_plain_ones();
  an_assertion_is_entered_or_not();
  a_nested_run_is_followed_at_depth_zero();
  an_echo_leaves_the_walk_incomplete();

}

}  // namespace lex_nfa_cases

namespace lex_dfa_cases {

namespace {

struct Built {
  std::uint16_t ndfa = 0;
  cursiv::Vec<std::uint16_t> dfaNext;
};

void large_subsets(bool redundant) {
  cursiv::ByteSet sets[3];
  sets[0].add('a');
  sets[1].add('a');
  sets[1].add('b');
  sets[2].add('x');

  cursiv::LexNfaPool nfa;
  const int accept = nfa.match(1);
  int tail = accept;
  for (unsigned i = 0; i < 16; ++i) tail = nfa.chars(1, tail);
  const int remembered = nfa.chars(0, tail);
  const int loop = nfa.hole();
  int head = nfa.split(nfa.chars(1, loop), remembered);
  // The extra accept makes every history equivalent.
  if (redundant) head = nfa.split(head, accept);
  nfa.s[loop].t1 = head;
  const int root = nfa.chars(2, loop);

  const cursiv::LexKeywordCut cut;
  const cursiv::TermInfo terms[2];
  cursiv::LexDfaBuild dfa(nfa, sets, 3,
                          {unsigned('a'), unsigned('b'), unsigned('x')}, 3, cut,
                          terms);
  dfa.close(root);
  const auto seed = dfa.intern();
  Built built;
  std::vector<bool> accepting;
  const auto write = [&](std::uint16_t id, const cursiv::LexDfaReading& r) {
    if (accepting.size() <= id) accepting.resize(id + 1);
    accepting[id] = r.acc.test(1);
  };

  bool refused = false;
  try {
    dfa.fill(built, write);
  } catch (const cursiv::LowerRefused& e) {
    refused = e.why == cursiv::LowerFail::tokenGraphTooLarge;
  }
  CHECK(refused == !redundant);
  if (refused || !redundant) return;

  CHECK(built.ndfa == 2);
  const auto start = dfa.final_of(seed);
  CHECK(!accepting[start]);
  const auto x = built.dfaNext[std::size_t{start} * 3 + 2];
  CHECK(x != cursiv::kDeadState && accepting[x]);
  if (x == cursiv::kDeadState) return;
  CHECK(built.dfaNext[std::size_t{x} * 3] == x &&
        built.dfaNext[std::size_t{x} * 3 + 1] == x);
  CHECK(built.dfaNext[std::size_t{start} * 3] == cursiv::kDeadState &&
        built.dfaNext[std::size_t{x} * 3 + 2] == cursiv::kDeadState);
}

struct Wide {
  cursiv::ByteSet sets[4];
  cursiv::LexNfaPool nfa;
  int root = -1;

  Wide() {
    sets[0].add('a');
    sets[1].add(0xC3);
    sets[2].add(0xA9);
    sets[3].add('x');
  }

  std::uint16_t read(Built& built, std::vector<bool>& accepting) {
    const cursiv::LexKeywordCut cut;
    const cursiv::TermInfo terms[2];
    cursiv::LexDfaBuild dfa(nfa, sets, 4,
                            {unsigned('a'), 0xC3u, 0xA9u, unsigned('x')}, 4,
                            cut, terms);
    dfa.close(root);
    const auto seed = dfa.intern();
    dfa.fill(built, [&](std::uint16_t id, const cursiv::LexDfaReading& r) {
      if (accepting.size() <= id) accepting.resize(id + 1);
      accepting[id] = r.acc.test(1);
    });
    return dfa.final_of(seed);
  }
};

std::uint16_t step(const Built& built, std::uint16_t from, unsigned cls) {
  return built.dfaNext[std::size_t{from} * 4 + cls];
}

void a_loop_over_every_character_is_one_state() {
  Wide w;
  auto& nfa = w.nfa;
  const auto accept = nfa.match(1);
  const auto loop = nfa.hole();
  const auto pending = nfa.hole();
  const auto lead = nfa.chars(1, pending);
  nfa.s[pending].t1 = nfa.split(
      nfa.chars(2, loop),
      nfa.split(nfa.chars(0, loop),
                nfa.split(nfa.chars(1, pending), nfa.match_strayed(1, 2, -1))));
  nfa.s[loop].t1 =
      nfa.split(nfa.chars(0, loop), nfa.split(nfa.chars(2, loop), lead));
  nfa.s[loop].t2 = accept;
  w.root = nfa.chars(3, loop);

  Built built;
  std::vector<bool> accepting;
  const auto start = w.read(built, accepting);
  CHECK(built.ndfa == 2);
  const auto in = step(built, start, 3);
  CHECK(in != cursiv::kDeadState && accepting[in]);
  if (in == cursiv::kDeadState) return;
  for (unsigned cls = 0; cls < 3; ++cls) CHECK(step(built, in, cls) == in);
}

void a_character_before_a_byte_keeps_its_length() {
  Wide w;
  auto& nfa = w.nfa;
  const auto accept = nfa.match(1);
  const auto after = nfa.chars(0, accept);
  const auto pending = nfa.split(nfa.chars(2, after), nfa.chars(0, accept));
  const auto read = nfa.split(nfa.chars(1, pending), nfa.chars(2, after));
  w.root = nfa.chars(3, read);

  Built built;
  std::vector<bool> accepting;
  const auto start = w.read(built, accepting);
  CHECK(built.ndfa == 5);
  const auto in = step(built, start, 3);
  if (in == cursiv::kDeadState) return;
  CHECK(step(built, in, 1) != step(built, in, 2));
}

}  // namespace

void run() {
  large_subsets(true);
  large_subsets(false);
  a_loop_over_every_character_is_one_state();
  a_character_before_a_byte_keeps_its_length();

}

}  // namespace lex_dfa_cases

namespace lex_owed_rows_cases {

// The bytes a mode opens a body with: a token that begins behind a zero-width
// assertion is opened by the bytes that begin the rest of it.
namespace {

// 0 launches an assertion that resumes in 1; 1 reads `"` into 2, the body,
// which loops on `a` and reads `"` into 3, where the token is accepted.
struct Quoted {
  bool asserts;

  std::size_t states() const { return 4; }
  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    static constexpr std::uint16_t kEdges[][2] = {{}, {2, 0}, {2, 3}, {}};
    return {kEdges[s], s == 1 ? 1u : s == 2 ? 2u : 0u};
  }
  std::uint16_t accept(std::uint16_t s) const { return s == 3 ? 1 : 0; }
  std::uint16_t relay(std::uint16_t s) const {
    return s == 0 && asserts ? 0 : cursiv::kNoRelay;
  }
  std::vector<std::uint16_t> hops(std::uint16_t s) const {
    return s == 0 && asserts ? std::vector<std::uint16_t>{1}
                             : std::vector<std::uint16_t>{};
  }
  std::size_t modes() const { return 1; }
  std::uint16_t start(std::uint16_t) const { return 0; }
  std::uint16_t next(std::uint16_t s, unsigned char b) const {
    if (s == 1 && b == '"') return 2;
    if (s == 2 && b == 'a') return 2;
    if (s == 2 && b == '"') return 3;
    return cursiv::kDeadState;
  }
  bool before(unsigned a, unsigned b) const { return a < b; }
};

}  // namespace

void run() {
  const auto behind = cursiv::lex_owed_rows(Quoted{true});
  CHECK(behind.openers[0].has('"'));
  CHECK(behind.bodies[2] == 1 && behind.owed[2] == 0);
  const auto plain = cursiv::lex_owed_rows(Quoted{false});
  CHECK(!plain.openers[0].has('"'));

}

}  // namespace lex_owed_rows_cases

namespace lex_reserve_cases {

// The terminals a relay defers are the ones its launch state and every state
// reached from it may end on.
void run() {
  // 0 -> 1 -> 2 -> 2, 0 -> 3; a relay of terminal 5 launches at 1.
  const std::vector<std::vector<std::uint32_t>> edges = {{1, 3}, {2}, {2}, {}};
  const auto reach = cursiv::lex_relayed(
      edges.size(),
      [](std::size_t s, auto&& each) {
        if (s == 1) each(5u);
      },
      [&](std::size_t s, auto&& each) {
        for (const std::uint32_t to : edges[s]) each(to);
      });
  CHECK(reach.size() == 4);
  CHECK(!reach[0].any() && !reach[3].any());
  CHECK(reach[1].test(5) && reach[2].test(5));

}

}  // namespace lex_reserve_cases

int main() {
  cursiv::test::count_expectations();
  lex_nfa_cases::run();
  lex_dfa_cases::run();
  lex_owed_rows_cases::run();
  lex_reserve_cases::run();
  return cursiv::test::report("LexNfa_test");
}
