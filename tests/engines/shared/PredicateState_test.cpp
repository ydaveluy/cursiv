#include "cursiv/engines/shared/runtime/PredicateState.hpp"

#include <cstddef>
#include <utility>

#include "cursiv/engines/shared/runtime/LexicalMachine.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using cursiv::PredicateCursor;
using cursiv::PredicateState;

PredicateCursor at(cursiv::TextOffset p) { return PredicateCursor{p, p}; }

void a_set_counts_its_inline_cursor_and_its_tail() {
  PredicateState s;
  CHECK(s.empty() && s.size() == 0 && !s.has(at(1)));
  s.add(at(5));
  CHECK(!s.empty() && s.size() == 1 && s.has(at(5)));
  s.add(at(5));
  CHECK(s.size() == 1);
  s.add(at(9));
  s.add(at(7));
  s.add(at(7));
  CHECK(s.size() == 3 && s[0] == at(5) && s[1] == at(7) && s[2] == at(9));
  CHECK(s.has(at(7)) && s.has(at(9)) && !s.has(at(8)));
}

void unite_and_without_keep_the_size_exact() {
  PredicateState a, b;
  for (const auto p : {2u, 4u, 6u}) a.add(at(p));
  for (const auto p : {1u, 4u, 5u, 6u, 8u}) b.add(at(p));
  PredicateState u = a;
  u.unite(b);
  CHECK(u.size() == 6);
  for (const auto p : {1u, 2u, 4u, 5u, 6u, 8u}) CHECK(u.has(at(p)));
  const PredicateState d = u.without(a);
  CHECK(d.size() == 3 && d.has(at(1)) && d.has(at(5)) && d.has(at(8)));
  CHECK(u == u && !(u == a) && !(a == b));
  PredicateState e;
  e.unite(a);
  CHECK(e == a);
}

// A moved-from set owes nothing but a valid state: it must never claim
// elements its storage no longer holds.
void a_moved_from_set_stays_within_its_storage() {
  PredicateState a;
  for (const auto p : {3u, 4u, 5u}) a.add(at(p));
  PredicateState b = std::move(a);
  CHECK(b.size() == 3 && b.has(at(5)));
  for (std::size_t i = 0; i < a.size(); ++i) (void)a[i];
  (void)a.has(at(4));
  (void)a.without(b);
  a.unite(b);
  CHECK(a.size() == 3);
  PredicateState c;
  c.add(at(1));
  c = std::move(b);
  CHECK(c.size() == 3);
  for (std::size_t i = 0; i < b.size(); ++i) (void)b[i];
  b = PredicateState{};
  CHECK(b.empty());
}

void a_machine_hands_back_the_frame_it_opens() {
  cursiv::LexicalMachine m;
  auto& f = m.enter(7u, {});
  CHECK(&f == &m.frame() && f.node == 7u);
}

void tokens_compare_by_span_and_symbol_not_mode() {
  cursiv::Token a{1, 2, 3, 0}, b{1, 2, 3, 4};
  CHECK(a == b);
  b.sym = 4;
  CHECK(!(a == b));
  static_assert(cursiv::kBacktrackTail == 64);
}

}  // namespace

int main() {
  a_set_counts_its_inline_cursor_and_its_tail();
  unite_and_without_keep_the_size_exact();
  a_moved_from_set_stays_within_its_storage();
  a_machine_hands_back_the_frame_it_opens();
  tokens_compare_by_span_and_symbol_not_mode();
  return cursiv::test::report("PredicateState_test");
}
