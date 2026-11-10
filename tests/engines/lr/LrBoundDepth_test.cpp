#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrParser.hpp"
#include "cursiv/lr/runtime/LrSearch.hpp"
#include "cursiv/lr/runtime/lr_search.hpp"
#include "cursiv/lr/runtime/lr_search_bound.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/Check.hpp"
#include "engines/lr/LrStand.hpp"

// What a reading still needs is walked over the text ahead as far as the bound
// reaches, on a stack of its own. Checked against a reference recursion, same
// values and work, and against a native stack too small for that recursion.

namespace {

namespace ga = ::arith::generated::Arith;

using checks::kUnbounded;

using checks::Stand;

// The two calls as they were written: each a native frame.
namespace ref {

inline int depth = 0;
inline int deepest = 0;

template <const auto& A>
std::uint32_t cost_ahead(const cursiv::engine::LrContext& c,
                         cursiv::engine::LrSearch& s, cursiv::TextOffset p,
                         cursiv::engine::SymbolId prev, std::uint32_t run);

template <const auto& A>
std::uint32_t dropped(const cursiv::engine::LrContext& c,
                      cursiv::engine::LrSearch& s,
                      const cursiv::engine::Token& t,
                      cursiv::engine::SymbolId prev) {
  using namespace cursiv;
  using namespace cursiv::engine;
  if (t.begin >= c.len) return kLrNoBound;
  const TextOffset end =
      t.sym == kInvalidSymbol ? t.end : (t.end > t.begin ? t.end : t.begin + 1);
  if (lr_search_ahead<A>(c, s, t.begin).refused) return kLrRereadCost;
  std::uint32_t best =
      kLrEditCost + kLrLost + cost_ahead<A>(c, s, end, prev, 0);
  for (const Typo& y : lr_search_misread<A>(c, s, t.begin))
    if (best > kLrRereadCost)
      best =
          std::min(best, kLrRereadCost + cost_ahead<A>(c, s, y.end, y.term, 0));
  return best;
}

template <const auto& A>
std::uint32_t cost_ahead(const cursiv::engine::LrContext& c,
                         cursiv::engine::LrSearch& s, cursiv::TextOffset p,
                         cursiv::engine::SymbolId prev, std::uint32_t run) {
  using namespace cursiv;
  using namespace cursiv::engine;
  if (p >= s.horizon || lr_search_spent(s)) return 0;
  const std::uint64_t key =
      (std::uint64_t{p} << 20) | (std::uint64_t{prev} << 2) | run;
  if (const std::uint32_t* hit = s.costAhead.find(key)) return *hit;
  ++depth;
  deepest = std::max(deepest, depth);
  std::uint32_t best = kLrNoBound;
  const auto next = [&](const Token& t, bool again) {
    const bool follows =
        prev == kInvalidSymbol || (!again && A.derived.after[prev].test(t.sym));
    const std::uint32_t cost = follows ? 0 : kLrEditCost;
    if (cost >= best) return;
    const std::uint32_t held = follows ? run + 1 : 1;
    if (t.sym == kEofSymbol || held >= kRepairValidateShifts) {
      best = cost;
      return;
    }
    best = std::min(best, cost + cost_ahead<A>(c, s, t.end, t.sym, held));
  };
  lr_search_each<A>(c, s, p, next, [&](const Token& t) {
    if (best > kLrRereadCost) best = std::min(best, dropped<A>(c, s, t, prev));
  });
  if (!s.boundShort) s.costAhead.emplace(key).first = best;
  --depth;
  return best;
}

}  // namespace ref

// A search set out on the refusal `text` reaches, as a bound reads it.
template <const auto& A>
struct Bound {
  Stand<A> st;
  cursiv::engine::LrSearch s;

  Bound(std::string_view text, std::uint64_t share)
      : st(text, ga::kDrive), s(text.size(), kUnbounded, st.c.indentArena) {
    CHECK(ga::kDrive.enter(st.c) == cursiv::engine::LrExit::pause);
    cursiv::engine::lr_search_start(st.c, s);
    s.bounding = true;
    s.boundShort = false;
    if (share != ~std::uint64_t{0}) s.limit = s.work + share;
  }
};

constexpr std::uint64_t kAll = ~std::uint64_t{0};

// The same probes on two searches that began the same: one asks the walk, the
// other the reference recursion. Values, work spent and what the bound knows
// must agree after each.
template <const auto& A>
void agree(std::string_view text, std::uint64_t share) {
  using namespace cursiv;
  using namespace cursiv::engine;
  Bound<A> now(text, share);
  Bound<A> was(text, share);
  const TextOffset at = now.st.c.la.begin;
  std::vector<SymbolId> prevs{kInvalidSymbol};
  for (SymbolId q = 1; q < A.cfg.nterms && q < 6; ++q) prevs.push_back(q);
  const std::size_t span = text.size() - at;
  for (const TextOffset p :
       {at, static_cast<TextOffset>(at + 1), static_cast<TextOffset>(at + 2),
        static_cast<TextOffset>(at + span / 2),
        static_cast<TextOffset>(at + span - 1)})
    for (const SymbolId prev : prevs)
      for (std::uint32_t run = 0; run < 3; ++run) {
        const std::uint32_t a =
            lr_search_cost_ahead<A>(now.st.c, now.s, p, prev, run);
        const std::uint32_t b =
            ref::cost_ahead<A>(was.st.c, was.s, p, prev, run);
        CHECK(a == b);
        CHECK(now.s.work == was.s.work);
        CHECK(now.s.boundShort == was.s.boundShort);
        if (a != b || now.s.work != was.s.work) {
          std::printf("  `%.20s...` at %u prev %u run %u share %llu\n",
                      text.data(), p, static_cast<unsigned>(prev), run,
                      static_cast<unsigned long long>(share));
          return;
        }
      }
  // The token the refusal stands on, dropped.
  const Token t = now.st.c.la;
  for (const SymbolId prev : prevs) {
    const std::uint32_t a =
        lr_search_dropped<A>(now.st.c, now.s, t, prev);
    const std::uint32_t b = ref::dropped<A>(was.st.c, was.s, t, prev);
    CHECK(a == b);
    CHECK(now.s.work == was.s.work);
  }
}

// Native stack: a walk over `text` in a thread whose stack the reference
// recursion overruns, at a few hundred frames. An unoptimized build needs room
// for the parse alone.
#if defined(__linux__)
#ifdef NDEBUG
constexpr std::size_t kSmallStack = 32 * 1024;
#else
constexpr std::size_t kSmallStack = 128 * 1024;
#endif

struct SmallStackJob {
  std::string_view text;
  std::uint32_t costAhead = 0;
};

void* run_on_small_stack(void* arg) {
  auto* job = static_cast<SmallStackJob*>(arg);
  Bound<ga::kTable> b(job->text, kAll);
  job->costAhead = cursiv::engine::lr_search_cost_ahead<ga::kTable>(
      b.st.c, b.s, b.st.c.la.begin, cursiv::engine::kInvalidSymbol, 0);
  return nullptr;
}

void a_small_stack_holds_the_walk(std::string_view text) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, kSmallStack);
  SmallStackJob job{text};
  pthread_t th;
  if (pthread_create(&th, &attr, &run_on_small_stack, &job) != 0) {
    pthread_attr_destroy(&attr);
    return;
  }
  pthread_join(th, nullptr);
  pthread_attr_destroy(&attr);
  Bound<ga::kTable> b(text, kAll);
  CHECK(job.costAhead ==
        cursiv::engine::lr_search_cost_ahead<ga::kTable>(
            b.st.c, b.s, b.st.c.la.begin, cursiv::engine::kInvalidSymbol, 0));
}
#endif

std::string repeated(std::string_view head, std::string_view unit,
                     std::size_t n) {
  std::string out(head);
  for (std::size_t i = 0; i < n; ++i) out += unit;
  return out;
}

void the_walk_is_the_recursion_without_its_frames() {
  const std::string texts[] = {
      // Tokens of two bytes that never follow one another.
      repeated("module m ", "1 ", 400),
      // Tokens of one byte the lexer does not read.
      repeated("module m 1 ", "@", 700),
      repeated("module m 1 2 ", "\x01", 700),
      // Both, and brackets in between.
      repeated("module m 1 ", "1 ) ( @ ", 120),
      "module m f(1 2);",
  };
  int deepest = 0;
  for (const std::string& text : texts)
    for (const std::uint64_t share : {kAll, std::uint64_t{0}, std::uint64_t{7},
                                      std::uint64_t{40}, std::uint64_t{300}}) {
      agree<ga::kTable>(text, share);
      deepest = std::max(deepest, ref::deepest);
    }
  // The recursion was as deep as the text: the check above is not idle.
  CHECK(deepest >= 200);
#if defined(__linux__)
  a_small_stack_holds_the_walk(texts[0]);
  a_small_stack_holds_the_walk(texts[1]);
#endif
}

// A stop asked while the walk runs is read within one run of polls, the
// tokens ahead of it already read.
void a_stop_is_read_while_the_walk_runs() {
  const std::string text = repeated("module m ", "1 ", 400);
  Bound<ga::kTable> b(text, kAll);
  const auto& tokens =
      cursiv::engine::lr_search_ahead<ga::kTable>(b.st.c, b.s, b.st.c.la.begin)
          .tokens;
  CHECK(!tokens.empty());
  for (cursiv::TextOffset p = b.st.c.la.begin; p < text.size(); ++p)
    (void)cursiv::engine::lr_search_ahead<ga::kTable>(b.st.c, b.s, p);
  cursiv::CancellationTokenSource source;
  source.request_stop();
  const cursiv::CancellationToken token = source.get_token();
  b.st.c.stop = cursiv::engine::CancellationPoll(token);
  bool told = false;
  try {
    (void)cursiv::engine::lr_search_cost_ahead<ga::kTable>(
        b.st.c, b.s, b.st.c.la.begin, cursiv::engine::kInvalidSymbol, 0);
  } catch (const cursiv::OperationCancelled&) {
    told = true;
  }
  CHECK(told);
}

}  // namespace

int main() {
  a_stop_is_read_while_the_walk_runs();
  the_walk_is_the_recursion_without_its_frames();
  return cursiv::test::report("LrBoundDepth_test");
}
