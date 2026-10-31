#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/lr_search.hpp"
#include "cursiv/engines/lr/runtime/lr_search_bound.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/Check.hpp"

// What a reading still owes is walked over the text ahead of it, as far as the
// bound reaches: a text of one-byte or unreadable tokens makes the walk as
// deep as that reach. It runs on a stack of its own, so the depth is not the
// native stack's. Checked against the recursion it replaced, kept here as the
// reference: the same values, and the same work spent, whatever the share left
// the walk, and against a native stack too small for that recursion.

namespace {

namespace ga = ::arith::generated::Arith;

constexpr cursiv::RepairBudget kUnbounded{.floor = ~std::uint64_t{0},
                                          .perByte = 0,
                                          .perSearch = ~std::uint64_t{0},
                                          .perParse = ~std::uint64_t{0}};

// A context standing where a parse of `text` stands before a byte is read.
template <const auto& A>
struct Stand {
  cursiv::Cst cst;
  std::optional<cursiv::CstBuilder> builder;
  std::optional<cursiv::LrContext> c;
  std::vector<cursiv::Scan> room;

  Stand(std::string_view text, const cursiv::LrDrive& drive) {
    cst.reset(text);
    builder.emplace(cst);
    builder->reserve_stream(cst.text().size());
    c.emplace(cst.text(), *builder);
    room.resize(A.maxCands != 0 ? A.maxCands : 1);
    c->cand = room.data();
    c->drive = &drive;
    cursiv::LrParser<A>::install_hooks(*c);
    c->stack.push(
        cursiv::LrFrame{0, cursiv::kNoElement, 0, 0, cursiv::kLrNoWord});
  }
};

// The two calls as they were written: each a native frame.
namespace ref {

inline int depth = 0;
inline int deepest = 0;

template <const auto& A>
std::uint32_t owes(const cursiv::LrContext& c, cursiv::LrSearch& s,
                   cursiv::TextOffset p, cursiv::SymbolId prev,
                   std::uint32_t run);

template <const auto& A>
std::uint32_t dropped(const cursiv::LrContext& c, cursiv::LrSearch& s,
                      const cursiv::Token& t, cursiv::SymbolId prev) {
  using namespace cursiv;
  if (t.begin >= c.len) return kLrNoBound;
  const TextOffset end =
      t.sym == kInvalidSymbol ? t.end : (t.end > t.begin ? t.end : t.begin + 1);
  if (lr_search_ahead<A>(c, s, t.begin).refused) return kLrOwedReread;
  std::uint32_t best =
      kLrOwedGesture + kLrLost + owes<A>(c, s, end, prev, 0);
  for (const Typo& y : lr_search_misread<A>(c, s, t.begin))
    if (best > kLrOwedReread)
      best = std::min(best, kLrOwedReread + owes<A>(c, s, y.end, y.term, 0));
  return best;
}

template <const auto& A>
std::uint32_t owes(const cursiv::LrContext& c, cursiv::LrSearch& s,
                   cursiv::TextOffset p, cursiv::SymbolId prev,
                   std::uint32_t run) {
  using namespace cursiv;
  if (p >= s.horizon || lr_search_spent(s)) return 0;
  const std::uint64_t key =
      (std::uint64_t{p} << 20) | (std::uint64_t{prev} << 2) | run;
  if (const std::uint32_t* hit = s.owes.find(key)) return *hit;
  ++depth;
  deepest = std::max(deepest, depth);
  std::uint32_t best = kLrNoBound;
  const auto next = [&](const Token& t, bool again) {
    const bool follows =
        prev == kInvalidSymbol || (!again && A.derived.after[prev].test(t.sym));
    const std::uint32_t cost = follows ? 0 : kLrOwedGesture;
    if (cost >= best) return;
    const std::uint32_t held = follows ? run + 1 : 1;
    if (t.sym == kEofSymbol || held >= kRepairValidateShifts) {
      best = cost;
      return;
    }
    best = std::min(best, cost + owes<A>(c, s, t.end, t.sym, held));
  };
  lr_search_each<A>(c, s, p, next, [&](const Token& t) {
    if (best > kLrOwedReread) best = std::min(best, dropped<A>(c, s, t, prev));
  });
  if (!s.boundShort) s.owes.emplace(key).first = best;
  --depth;
  return best;
}

}  // namespace ref

// A search set out on the refusal `text` meets, as a bound reads it.
template <const auto& A>
struct Bound {
  Stand<A> st;
  cursiv::LrSearch s;

  Bound(std::string_view text, std::uint64_t share)
      : st(text, ga::kDrive), s(text.size(), kUnbounded, st.c->indentArena) {
    CHECK(ga::kDrive.enter(*st.c) == cursiv::LrExit::pause);
    cursiv::lr_search_start(*st.c, s);
    s.bounding = true;
    s.boundShort = false;
    if (share != ~std::uint64_t{0}) s.limit = s.work + share;
  }
};

constexpr std::uint64_t kAll = ~std::uint64_t{0};

// The same probes on two searches that began alike: one asks the walk now
// written, the other the recursion. Values, work spent and what the bound
// knows must agree after each.
template <const auto& A>
void agree(std::string_view text, std::uint64_t share) {
  using namespace cursiv;
  Bound<A> now(text, share);
  Bound<A> was(text, share);
  const TextOffset at = now.st.c->la.begin;
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
            lr_search_owes<A>(*now.st.c, now.s, p, prev, run);
        const std::uint32_t b = ref::owes<A>(*was.st.c, was.s, p, prev, run);
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
  const Token t = now.st.c->la;
  for (const SymbolId prev : prevs) {
    const std::uint32_t a =
        lr_search_dropped<A>(*now.st.c, now.s, t, prev);
    const std::uint32_t b = ref::dropped<A>(*was.st.c, was.s, t, prev);
    CHECK(a == b);
    CHECK(now.s.work == was.s.work);
  }
}

// Native stack: a walk over `text` in a thread whose stack the recursion it
// replaced overran (it segfaults at this size), at a few hundred frames. An
// unoptimized build needs room for the parse alone, whose frames are not
// shrunk.
#if defined(__linux__)
#ifdef NDEBUG
constexpr std::size_t kCrampedStack = 32 * 1024;
#else
constexpr std::size_t kCrampedStack = 128 * 1024;
#endif

struct Cramped {
  std::string_view text;
  std::uint32_t owed = 0;
};

void* cramped(void* arg) {
  auto* job = static_cast<Cramped*>(arg);
  Bound<ga::kTable> b(job->text, kAll);
  job->owed = cursiv::lr_search_owes<ga::kTable>(
      *b.st.c, b.s, b.st.c->la.begin, cursiv::kInvalidSymbol, 0);
  return nullptr;
}

void a_small_stack_holds_the_walk(std::string_view text) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, kCrampedStack);
  Cramped job{text};
  pthread_t th;
  if (pthread_create(&th, &attr, &cramped, &job) != 0) {
    pthread_attr_destroy(&attr);
    return;
  }
  pthread_join(th, nullptr);
  pthread_attr_destroy(&attr);
  Bound<ga::kTable> b(text, kAll);
  CHECK(job.owed == cursiv::lr_search_owes<ga::kTable>(
                        *b.st.c, b.s, b.st.c->la.begin, cursiv::kInvalidSymbol,
                        0));
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

}  // namespace

int main() {
  the_walk_is_the_recursion_without_its_frames();
  return cursiv::test::report("LrBoundDepth_test");
}
