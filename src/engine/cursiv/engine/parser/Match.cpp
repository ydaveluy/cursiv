// The matcher: reads a terminal the lexer's automaton reads the first bytes
// of only, every way the text allows.
#include "cursiv/engine/parser/Match.hpp"

#include <deque>
#include <iterator>
#include <limits>

namespace cursiv::engine {

// The searches under way, the innermost condition's last; and by mark of the
// token read, the furthest text found to repeat its first bytes, from `from`
// to `to`, and the Z-function of its first `z.size()` bytes.
struct Machine::Matchings {
  std::deque<Matching> searches;
  struct Echo {
    TextOffset from = 0;
    TextOffset to = 0;
    std::vector<TextOffset> z;
  };
  std::map<std::uint64_t, Echo> echoes;
  // By entry and place, read with no mark: the ends a search of the token
  // found, and whether a search read it every way. What is known may rest on
  // the end of the text where the token read it since `reads`.
  struct Known {
    std::map<TextOffset, TextOffset> ends;
    bool whole = false;
  };
  std::map<std::uint64_t, Known> known;
  std::uint64_t reads = 0;
};

// Reads as the Z-algorithm: inside the furthest repeat of the mark found, the
// mark's own Z-function gives what repeats, and only bytes past it are read.
bool Matching::echoes(Machine& m, MatchMark mark, TextOffset at) {
  if (mark.end == kNoMark) return false;
  const auto length = mark.end - mark.begin;
  const char* const captured = m.src + mark.begin;
  const auto available = static_cast<TextOffset>(m.end - m.src) - at;
  auto& known = m.matchings->echoes[std::uint64_t{mark.begin} << 32 | mark.end];
  // How many bytes of the mark `x`, at place `i`, repeats, up to `limit`; the
  // furthest repeat found, from `from` to `to`, moves on past them.
  const auto repeats = [&](TextOffset& from, TextOffset& to, const char* x, TextOffset i,
                           TextOffset limit) {
    TextOffset k = from <= i && i < to ? std::min(to - i, known.z[i - from]) : 0;
    while (k < limit) {
      const char* const next = m.watched(x + k, x + limit);
      const char* const differs = std::mismatch(x + k, next, captured + k).first;
      k = static_cast<TextOffset>(differs - x);
      if (differs != next) break;
    }
    if (i + k > to) {
      from = i;
      to = i + k;
    }
    return k;
  };
  if (const TextOffset w = known.to - known.from;
      known.from <= at && at < known.to && known.z.size() < w) {
    const TextOffset size = w + std::min(w, length - w);
    known.z.assign(size, size);
    for (TextOffset i = 1, l = 0, r = 0; i < size; ++i) {
      watch();
      known.z[i] = repeats(l, r, captured + i, i, size - i);
    }
  }
  const TextOffset same =
      repeats(known.from, known.to, m.src + at, at, std::min(length, available));
  if (same == length) return true;
  m.observe_end(same == available);
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
  const std::string_view input(at, m.end);
  const std::size_t length = a.scans[e.arg](input);
  return length <= input.size() ? length : 0;
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

const char* Machine::match(std::uint32_t entry, const char* begin) {
  if (const char* found; Matching::alone(*this, entry, begin, found)) return found;
  const auto from = static_cast<TextOffset>(begin - src);
  const std::uint64_t place = stretch_place(kUnmatched, entry, from);
  if (unmatched(place)) return nullptr;
  if (!matchings) matchings = {new Matchings, [](Matchings* gone) { delete gone; }};
  matchings->echoes.clear();
  matchings->known.clear();
  matchings->reads = endReads;
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
}

}  // namespace cursiv::engine
