// The matcher: reads a terminal the lexer's automaton reads the first bytes
// of only, every way the text allows.
#include "cursiv/engine/parser/Match.hpp"

#include <deque>
#include <iterator>
#include <limits>

namespace cursiv::engine {

// The searches under way, the innermost condition's last; and by start of a
// mark of the token read, the Z-function of the first `bound` bytes of text
// there, as far as places were asked, with its furthest repeat, `from`-`to`.
struct Machine::Matchings {
  std::deque<Matching> searches;
  struct Scan {
    const char* begin;
    const char* end;
    std::uint32_t index;
    std::size_t length;
  } scan{};
  struct Echo {
    std::vector<TextOffset> z;
    TextOffset bound = 0;
    TextOffset from = 0;
    TextOffset to = 0;
  };
  std::map<TextOffset, Echo> echoes;
  // By entry and place, read with no mark: the ends a search of the token
  // found, and whether a search read it every way. What is known may rest on
  // the end of the text where the token read it since `reads`.
  struct Known {
    std::map<TextOffset, TextOffset> ends;
    bool whole = false;
  };
  std::map<std::uint64_t, Known> known;
  std::uint64_t reads = 0;
  // The steps the token's searches took, from `from`, and the furthest place
  // a thread of theirs read to.
  std::uint64_t spent = 0;
  TextOffset from = 0;
  TextOffset reach = 0;
};

namespace {

// The work the matcher spends on one terminal at one place: steps per byte of
// the text its threads read, and steps for any text. A terminal that needs
// more, as one whose calls each end at many places can, is not read there.
constexpr std::uint64_t kStepsPerByte = 64;
constexpr std::uint64_t kSteps = std::uint64_t{1} << 20;
struct Overspent {};

}  // namespace

void Matching::charge() {
  throwIfCancelled(_machine->stop);
  if (Machine::Matchings* const all = _machine->matchings.get();
      all != nullptr && (all->spent += 1024) > kStepsPerByte * (all->reach - all->from + 1) + kSteps)
    throw Overspent{};
}

// Every mark from one start is a prefix of the text there: its Z-function, read
// left to right, tells each place, asked in any order, how much of it repeats.
// A mark ends by the place asked, so the text up to twice that place is enough.
bool Matching::echoes(Machine& m, MatchMark mark, TextOffset at) {
  if (mark.end == kNoMark) return false;
  const char* const start = m.src + mark.begin;
  const auto text = static_cast<TextOffset>(m.end - start);
  const TextOffset place = at - mark.begin;
  Machine::Matchings::Echo& known = m.matchings->echoes[mark.begin];
  if (const TextOffset needs = place + std::min(place, text - place);
      known.z.empty() || known.bound < needs) {
    const TextOffset bound =
        std::max(needs, known.bound + std::min(known.bound, text - known.bound));
    known = {{bound}, bound};
  }
  for (auto i = static_cast<TextOffset>(known.z.size()); i <= place;) {
    // Past the furthest repeat, a place that does not begin as the text does
    // repeats nothing.
    if (const char* const limit = start + std::min(place + 1, known.bound);
        i >= known.to && start + i < limit) {
      const char* const edge = m.watched(start + i, limit);
      const auto* hit = static_cast<const char*>(std::memchr(start + i, *start, edge - start - i));
      if (hit != start + i) {
        i = static_cast<TextOffset>((hit != nullptr ? hit : edge) - start);
        known.z.resize(i, 0);
        continue;
      }
    }
    watch();
    TextOffset k = i < known.to ? std::min(known.to - i, known.z[i - known.from]) : 0;
    while (i + k < known.bound) {
      const char* const next = m.watched(start + i + k, start + known.bound);
      const char* const differs = std::mismatch(start + i + k, next, start + k).first;
      k = static_cast<TextOffset>(differs - start - i);
      if (differs != next) break;
    }
    known.z.push_back(k);
    if (i + k > known.to) {
      known.from = i;
      known.to = i + k;
    }
    ++i;
  }
  const TextOffset length = mark.end - mark.begin, same = known.z[place];
  if (same >= length) return true;
  m.observe_end(place + same == text);
  return false;
}

namespace {

bool in_set(const std::uint64_t* set, unsigned byte) {
  return ((set[byte >> 6] >> (byte & 63)) & 1) != 0;
}

}  // namespace

CURSIV_ALWAYS_INLINE inline bool Matching::holds(Machine& m, const MatchEnd& x, const char* p) {
  return x.guard == LexTest::none ||
         (x.guard == LexTest::eof ? m.observe_end(p == m.end) : m.settles(x.waits, p));
}

CURSIV_ALWAYS_INLINE inline std::size_t Matching::scanned(Machine& m, const MatchTables& a,
                                                          const MatchExit& e, const char* at) {
  m.observe_end(true);
  auto* const matchings = m.matchings.get();
  if (matchings && matchings->scan.begin == at && matchings->scan.end == m.end &&
      matchings->scan.index == e.arg) return matchings->scan.length;
  const std::string_view input(at, m.end);
  const std::size_t length = a.scans[e.arg](input);
  const auto matched = length <= input.size() ? length : 0;
  if (matchings) matchings->scan = {at, m.end, e.arg, matched};
  return matched;
}

CURSIV_ALWAYS_INLINE inline void Matching::leave(Machine& m, const MatchTables& a,
                                                 const MatchThread& th, const MatchExit& e,
                                                 TextOffset at) {
  const auto to = [&](std::uint32_t entry, TextOffset place) {
    MatchThread next = th;
    next.entry = entry;
    next.at = place;
    return next;
  };
  // A thread its next byte would end at once is not made.
  if (e.first != 0 && (m.observe_end(m.src + at == m.end) ||
                       !in_set(a.sets + 4 * (e.first - 1u), byte_at(m.src + at))))
    return;
  switch (e.action) {
    case MatchAction::end:
      end(th.call, at);
      return;
    case MatchAction::jump: {
      MatchThread next;
      next.entry = e.arg;
      next.at = at;
      next.call = th.call;
      go(next);
      return;
    }
    case MatchAction::open: {
      MatchThread next = to(e.to, at);
      next.marks[e.arg] = {at, kNoMark};
      go(next);
      return;
    }
    case MatchAction::close: {
      if (th.marks[e.arg].end != kNoMark || th.marks[e.arg].begin == kNoMark) return;
      MatchThread next = to(e.to, at);
      next.marks[e.arg].end = at;
      go(next);
      return;
    }
    case MatchAction::scan: {
      const std::size_t length = scanned(m, a, e, m.src + at);
      if (length == 0) return;
      const auto past = at + static_cast<TextOffset>(length);
      if (e.ends)
        end(th.call, past, true);
      else
        go(to(e.to, past));
      return;
    }
    case MatchAction::echo:
      if (const MatchMark mark = th.marks[e.arg]; echoes(m, mark, at))
        go(to(e.to, at + (mark.end - mark.begin)));
      return;
    case MatchAction::call:
      call(m, e.arg, to(e.to, at), e.suffix != 0 ? a.sets + 4 * (e.suffix - 1u) : nullptr);
      return;
    case MatchAction::ahead:
    case MatchAction::unless:
      ask(e.arg, e.action == MatchAction::unless, to(e.to, at));
      return;
  }
}

void Matching::run(Machine& m) {
  const MatchTables a = *m.t.match;
  const char* const src = m.src;
  const char* const end = m.end;
  while (runs()) {
    const MatchThread th = take();
    const char* p = src + th.at;
    const char* limit = stride(m, p);
    for (std::uint32_t state = a.entries[th.entry]; state != 0;) {
      const MatchState here = a.states[state];
      for (std::uint32_t k = here.ends, last = a.states[state + 1].ends; k != last; ++k) {
        const MatchEnd x = a.ends[k];
        if (!holds(m, x, p)) continue;
        leave(m, a, th, a.exits[x.exit], static_cast<TextOffset>(p - src - x.back));
      }
      if (here.row == kNoRow) break;
      if (here.run != 0) {
        const std::uint32_t run = here.run - 1u;
        const char* const bound = _made < kUnchecked ? m.mark : limit;
        const char* const r = m.t.runScanners[run](m.t.runSets + run * 256u, p, bound);
        p = r < bound ? r : m.run_on(run, r);
      }
      if (p >= limit) [[unlikely]]
        if (m.observe_end(p == end) || !passes(m, th, state, p, limit)) break;
      state = a.next.at(here.row, a.byteClass[byte_at(p++)]);
    }
    m.matchings->reach = std::max(m.matchings->reach, static_cast<TextOffset>(p - src));
  }
}

bool Matching::alone(Machine& m, std::uint32_t entry, const char* begin, const char*& found) {
  const MatchTables& a = *m.t.match;
  const char* p = begin;
  const char* const limit = stride(m, p);
  found = nullptr;
  for (std::uint32_t state = a.entries[entry]; state != 0;) {
    const MatchState here = a.states[state];
    for (std::uint32_t k = here.ends, last = a.states[state + 1].ends; k != last; ++k) {
      const MatchEnd x = a.ends[k];
      const MatchExit& e = a.exits[x.exit];
      const bool scans = e.action == MatchAction::scan && e.ends;
      if (e.action != MatchAction::end && !scans) return false;
      if (!holds(m, x, p)) continue;
      const char* at = p - x.back;
      if (scans) {
        const std::size_t length = scanned(m, a, e, at);
        if (length == 0) continue;
        at += length;
      }
      if (found == nullptr || at > found) found = at;
    }
    if (here.row == kNoRow) break;
    if (here.run != 0 || p >= limit) return false;
    state = a.next.at(here.row, a.byteClass[byte_at(p++)]);
  }
  return true;
}

void Matching::call(Machine& m, std::uint32_t callee, const MatchThread& then,
                    const std::uint64_t* suffix) {
  // A call starts with no mark, from the ends the token's searches found for
  // it; it reads nothing where a whole search found it read nothing, and is not
  // read again where a search read it every way.
  if (m.unmatched(stretch_place(kUnmatched, callee, then.at))) return;
  const auto [known, fresh] =
      _callOf.try_emplace({callee, then.at}, static_cast<std::uint32_t>(_calls.size()));
  const std::uint32_t called = known;
  if (fresh) {
    const auto found = m.matchings->known.find(stretch_place(kUnmatched, callee, then.at));
    const bool seen = found != m.matchings->known.end();
    if (seen) m.observe_end(m.endReads != m.matchings->reads);
    _calls.push_back({{callee, then.at}, kNoLink, seen ? found->second.ends : Ranges{}});
    MatchThread first;
    first.entry = callee;
    first.at = then.at;
    first.call = called;
    if (!seen || !found->second.whole) go(first);
  }
  Call& c = _calls[called];
  _callers.push_back({then, suffix, c.callers});
  c.callers = static_cast<std::uint32_t>(_callers.size() - 1);
  for (const auto& [first, last] : c.ends) resume(c.callers, first, last);
}

// Joins an inclusive range to the set, visiting only its new positions.
template <class Each>
void Matching::add(Ranges& ranges, TextOffset first, TextOffset last, Each each) {
  watch();
  auto from = ranges.upper_bound(first);
  if (from != ranges.begin() && std::prev(from)->second + 1 >= first) --from;
  if (from != ranges.end() && from->first <= first && from->second >= last) return;
  auto next = first;
  auto it = from;
  for (; it != ranges.end() && it->first <= last + 1; ++it) {
    watch();
    if (next < it->first) each(next, std::min(last, it->first - 1));
    next = std::max(next, it->second + 1);
  }
  if (next <= last) each(next, last);
  if (it != from) last = std::max(last, std::prev(it)->second);
  if (from != it && from->first <= first) {
    from->second = last;
    ranges.erase(std::next(from), it);
  } else {
    ranges.emplace_hint(ranges.erase(from, it), first, last);
  }
}

void Matching::ended(std::uint32_t call, TextOffset first, TextOffset last) {
  if (call == 0) return end(0, last);
  Call& c = _calls[call];
  add(c.ends, first, last, [&](TextOffset lo, TextOffset hi) {
    for (auto k = c.callers; k != kNoLink; k = _callers[k].next) resume(k, lo, hi);
  });
}

void Matching::resume(std::uint32_t caller, TextOffset first, TextOffset last) {
  Caller& c = _callers[caller];
  if (c.suffix) {
    if (c.pending.empty()) _pending.push_back(caller);
    add(c.pending, first, last, [](TextOffset, TextOffset) {});
  } else {
    MatchThread next = c.then;
    for (next.at = first; next.at <= last; ++next.at) {
      watch();
      go(next);
    }
  }
}

// An optional byte keeps every returned position in [a,b]. Only the byte
// at b can extend that range.
bool Matching::propagate(Machine& m) {
  if (!asked.empty() || (_any && found()) || _pending.empty()) return false;
  const auto caller = _pending.back();
  _pending.pop_back();
  Caller& c = _callers[caller];
  Ranges pending;
  pending.swap(c.pending);
  for (const auto& [first, last] : pending) {
    watch();
    const char* p = m.src + last;
    const bool reads = !m.observe_end(p == m.end) && in_set(c.suffix, byte_at(p));
    ended(c.then.call, first, last + reads);
  }
  return true;
}

void Matching::remember(Machine& m) {
  const bool atCut = m.endReads != _endReads;
  const bool whole = !_any || !found();
  for (std::size_t k = _any || found() ? 1u : 0u; k < _calls.size(); ++k) {
    watch();
    const Place p = _calls[k].called;
    const std::uint64_t key = stretch_place(kUnmatched, p.entry, p.at);
    if (whole && _calls[k].ends.empty()) m.stretches.try_emplace(key, p.at, atCut);
    if (!_any) continue;
    Machine::Matchings::Known& known = m.matchings->known[key];
    known.whole = known.whole || whole;
    for (const auto& [first, last] : _calls[k].ends)
      add(known.ends, first, last, [](TextOffset, TextOffset) {});
  }
  if (!whole) return;
  for (const auto key : _passed) {
    watch();
    const auto at = static_cast<TextOffset>(key);
    // A DFA checkpoint can still accept behind it after scanning a lexical condition.
    constexpr auto rewind = std::numeric_limits<decltype(LexResolution::back)>::max();
    if (!found() || (at > _furthest && at - _furthest > rewind))
      m.stretches.try_emplace(key, at, atCut);
    else if (!_any && _made == 1 && !_jumped && _furthest > at)
      m.stretches.try_emplace(key, _furthest, atCut);
  }
}

const char* Machine::match(std::uint32_t entry, const char* begin) try {
  if (const char* found; Matching::alone(*this, entry, begin, found)) return found;
  const auto from = static_cast<TextOffset>(begin - src);
  const std::uint64_t place = stretch_place(kUnmatched, entry, from);
  if (unmatched(place)) return nullptr;
  if (!matchings) matchings = {new Matchings, [](Matchings* gone) { delete gone; }};
  matchings->echoes.clear();
  matchings->known.clear();
  matchings->reads = endReads;
  matchings->spent = 0;
  matchings->from = matchings->reach = from;
  std::deque<Matching>& searches = matchings->searches;
  if (searches.empty()) searches.emplace_back();
  MatchThread first;
  first.entry = entry;
  first.at = from;
  searches[0].start(*this, first, false);
  // A condition's search goes on top of the one that asked it, and its answer
  // goes back once it is done; asked with no mark, it answers from what the
  // token's searches know of it.
  std::size_t top = 0;
  Matching* s = &searches[0];
  for (;;) {
    s->run(*this);
    if (s->propagate(*this)) continue;
    bool holds = false;
    if (!s->done()) {
      const Matching::Asked& asked = s->asked.back();
      const std::uint64_t key = stretch_place(kUnmatched, asked.entry, asked.then.at);
      const auto known = asked.then.marks == decltype(asked.then.marks){}
                             ? matchings->known.find(key)
                             : matchings->known.end();
      if (known == matchings->known.end()) {
        if (searches.size() == ++top) searches.emplace_back();
        MatchThread read = asked.then;
        read.entry = asked.entry;
        read.call = 0;
        searches[top].start(*this, read, true);
        s = &searches[top];
        continue;
      }
      holds = !known->second.ends.empty();
      observe_end(endReads != matchings->reads);
    } else {
      if (top == 0) break;
      holds = s->found();
      s->remember(*this);
      const TextOffset furthest = s->furthest();
      s = &searches[--top];
      const Matching::Asked& asked = s->asked.back();
      if (asked.then.marks == decltype(asked.then.marks){}) {
        auto& known = matchings->known[stretch_place(kUnmatched, asked.entry, asked.then.at)];
        if (!holds)
          known.whole = true;
        else if (known.ends.empty())
          known.ends.emplace(furthest, furthest);
      }
    }
    const Matching::Asked asked = s->asked.back();
    s->asked.pop_back();
    if (holds != asked.unless) s->go(asked.then);
  }
  s->remember(*this);
  return s->found() ? src + s->furthest() : nullptr;
} catch (const Overspent&) {
  // Read no further, the terminal is not read at this place again.
  const auto from = static_cast<TextOffset>(begin - src);
  stretches.try_emplace(stretch_place(kUnmatched, entry, from), from, endReads != matchings->reads);
  return nullptr;
}

}  // namespace cursiv::engine
