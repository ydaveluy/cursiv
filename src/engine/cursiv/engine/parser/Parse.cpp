#include "cursiv/engine/parser/Parse.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/engine/recovery/Repair.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

void Machine::flush() {
  const NodeCount before = tape.records();
  for (const Hidden& x : pending)
    if (x.entry == kSkippedElement) [[unlikely]]
      tape.repairedLeaf(x.begin, x.end, x.entry);
    else
      tape.hidden(x.begin, x.end, x.entry);
  pending.clear();
  const NodeCount after = tape.records();
  for (Frame* f = top; f != frames && f[-1].open == before; --f)
    f[-1].open = after;
}

void Machine::accept() {
  root = tape.records() - 1;
  flush();
  const auto skipped = [&](NodeCount id) { return tape.at(id).element() == kSkippedElement; };
  const auto apart = [&](NodeCount id) { return tape.at(id).isHidden() || skipped(id); };
  CstNode whole = tape.at(root);
  // A root that is one token, read or invented, stays that leaf.
  const bool token = whole.subtreeSize == 1 && (whole.begin != whole.end || whole.isRecovered());
  // The text skipped before it, back to the first of it: trivia read in
  // between is held with it.
  NodeCount first = root + 1 - whole.subtreeSize;
  for (NodeCount id = first; id != 0 && apart(id - 1); --id)
    if (skipped(id - 1)) {
      whole.begin = tape.at(id - 1).begin;
      whole.subtreeSize += first - (id - 1);
      first = id - 1;
    }
  // The text skipped after it, up to the last of it; all that follows where
  // the root read nothing.
  NodeCount last = root;
  const bool empty = whole.subtreeSize == 1 && whole.begin == whole.end;
  for (NodeCount id = root + 1; id != tape.records(); ++id)
    if (skipped(id)) {
      last = id;
      whole.end = tape.at(id).end;
    }
  if (token && (first != root || last != root)) {
    // The leaf and the text skipped around it go under a record of no
    // action, written after them.
    tape.leaf(0, 0, kNoElement);
    for (NodeCount id = tape.records() - 1; id != last + 1; --id) tape.at(id) = tape.at(id - 1);
    whole.subtreeSize = last - first + 2;
    whole.setElement(kNoElement);
    tape.at(last + 1) = whole;
    root = last + 1;
    return;
  }
  if (empty && last == root && first == root) {
    last = tape.records() - 1;
    whole.begin = whole.end = static_cast<TextOffset>(end - src);
  } else {
    whole.subtreeSize += last - root;
  }
  for (NodeCount id = root; id != last; ++id) tape.at(id) = tape.at(id + 1);
  tape.at(last) = whole;
  root = last;
}

void Machine::grow() {
  const std::size_t used = static_cast<std::size_t>(top - frames);
  const std::size_t room = used * 2;
  if (room > (std::size_t{1} << 28))
    throw std::length_error("cursiv: the parse stack is too deep");
  std::unique_ptr<Frame[]> wider(new Frame[room]);
  std::memcpy(wider.get(), frames, used * sizeof(Frame));
  wide = std::move(wider);
  frames = wide.get();
  top = frames + used;
  limit = frames + room;
}

std::size_t Machine::standing(NodeCount records) const {
  const Frame* from = top;
  while (from != frames && from[-1].open >= records) --from;
  if (from != frames) --from;
  return static_cast<std::size_t>(from - frames);
}

void Machine::checkpoint(Checkpoint& made) const {
  const std::size_t kept = std::min(standing(made.records), made.frames.size());
  made.frames.resize(kept);
  made.frames.insert(made.frames.end(), frames + kept, top);
  made.pending = pending;
  made.plan = plan;
  made.planned = planned;
  made.records = tape.records();
  if (made.records != 0) made.last = tape.at(made.records - 1);
  made.at = static_cast<TextOffset>(at - src);
  made.tok = tok;
  made.resume = resume;
  made.indented = indented;
}

void Machine::rewind(const Checkpoint& to) {
  std::ranges::copy(to.frames, frames);
  top = frames + to.frames.size();
  tape.truncate(to.records);
  if (to.records != 0) tape.at(to.records - 1) = to.last;
  pending = to.pending;
  plan = to.plan;
  planned = to.planned;
  at = src + to.at;
  tok = to.tok;
  resume = to.resume;
  indented = to.indented;
}

std::size_t Tables::wide_word_width(const char* p, const char* limit) const {
  std::size_t length = 0;
  const char32_t scalar = scalarAt(p, static_cast<std::size_t>(limit - p), length);
  if (scalar == kNoScalar) return wordInvalid ? 1 : 0;
  return unicode::within(wordRanges, wordRangeCount, scalar) ? length : 0;
}

void Machine::indent(Indented& at, Token tok, bool muted) {
  TextOffset begin = tok.begin(*this);
  TextOffset length = static_cast<TextOffset>(tok.end - src) - begin;
  if (length == 0 && at.level != 0) {
    const auto& outer = levels[at.level];
    begin = outer.begin;
    length = outer.length;
  }
  const Level opening{begin, length, at.level, muted};
  const auto key = opening.key();
  for (const Levels* known = levels.base ? levels.base : &levels;; known = &levels) {
    const auto ordered = std::span(known->opened).first(known->ordered);
    const bool follows = ordered.empty() || ordered.back().key() < key;
    if (!follows) {
      const auto it = std::ranges::lower_bound(ordered, key, {}, &Level::key);
      if (it->key() == key) {
        at.level = static_cast<std::uint32_t>(known->inherited() + (it - ordered.begin()) + 1);
        return;
      }
    }
    if (known == &levels) {
      at.level = static_cast<std::uint32_t>(levels.inherited() + levels.opened.size() + 1);
      if (follows && levels.ordered == levels.opened.size()) {
        ++levels.ordered;
      } else {
        const auto [it, fresh] = levels.rest.try_emplace(key, at.level);
        if (!fresh) { at.level = it->second; return; }
      }
      break;
    }
    if (const auto it = known->rest.find(key); it != known->rest.end()) {
      at.level = it->second;
      return;
    }
  }
  levels.opened.push_back(opening);
}

Token Machine::indentation(const char* p, Indented& at) {
  if (p != blankLine) {
    const char* run = p;
    const char* q = p;
    while (q != end) {
      const char* const edge = end - q > kWatchedBytes ? watched(q) : end;
      while (q != edge && (*q == ' ' || *q == '\t' || *q == '\f')) {
        if (*q == '\f') run = q + 1;
        ++q;
      }
      if (q != edge) break;
    }
    lineBlanks = {run, static_cast<std::size_t>(q - run)};
    blankLine = p;
  }
  const auto blanks = lineBlanks;
  const char* q = blanks.data() + blanks.size();
  if (q == end ? ended == kEndOfInput : *q == '\n' || *q == '\r') return Token{nullptr, 0};
  const auto spelled = [&](std::uint32_t l) {
    return l == 0 ? std::string_view{}
                  : std::string_view(src + levels[l].begin, levels[l].length);
  };
  const auto from = static_cast<TextOffset>(blanks.data() - src);
  const auto line = std::uint64_t{from} << 32 | blanks.size();
  const auto closing = t.dedentTerminal ? token(static_cast<TextOffset>(p - src), p, t.dedentTerminal)
                                        : Token{nullptr, 0};
  for (;;) {
    if (at.level && levels.closes(at.level, line) && !levels[at.level].muted)
      return closing;
    const std::string_view top = spelled(at.level);
    if (blanks == top) return Token{nullptr, 0};
    if (blanks.size() > top.size() && blanks.starts_with(top))
      return t.indentTerminal != 0 ? token(from, q, t.indentTerminal) : Token{nullptr, 0};
    if (at.level == 0 || !levels[at.level].muted) break;
    at.level = levels[at.level].outer;
  }
  for (auto l = at.level; l;) {
    const auto outer = levels[l].outer;
    if (spelled(outer) == blanks || levels.closes(l, line)) {
      for (auto k = at.level; k != outer; k = levels[k].outer) levels.close(k, line);
      return closing;
    }
    l = outer;
  }
  Token fault = token(from, q, t.terminalCount);
  if (!spelled(at.level).starts_with(blanks)) fault.head |= kMixedIndent;
  return fault;
}

bool Machine::gap_reads(std::uint32_t monitor, const char* from, const char* to,
                        bool whole) const {
  // One pass: the monitor of a part reads on from wherever a part begins.
  // The gap's end ends its last character, as the end of input does.
  const TextMonitor& m = t.monitors[monitor];
  std::uint32_t state = 1;
  while (from != to) {
    const char* const edge = to - from > kWatchedBytes ? watched(from) : to;
    for (; from != edge; ++from) {
      if (!whole && (m.flags[state] & 1) != 0) return true;
      state = m.next[state * m.classes + m.byteClass[byte_at(from)]];
      if (state == 0) return false;
    }
  }
  return (m.flags[state] & 2) != 0;
}

bool Machine::settles(std::uint32_t list, const char* p, LexWork* work) {
  for (std::uint32_t at = t.waitAt[list]; at != t.waitAt[list + 1]; at += 3) {
    const std::uint32_t monitor = t.waitWord[at];
    const TextMonitor& m = t.monitors[monitor];
    std::uint32_t state = t.waitWord[at + 2];
    // The monitor reads on until it matches, cannot any more, or meets a
    // place where an earlier reading in its state answered.
    PredicateResult result{};
    passed.clear();
    const auto left = work && work->read < work->limit ? work->limit - work->read : 0;
    const char* bound = work && left < static_cast<std::uint64_t>(end - p) ? p + left : end;
    const char* until = end - p > kWatchedBytes ? p + kWatchedBytes : end;
    const char* q = p;
    for (;; ++q) {
      if (q == bound) {
        if (work != nullptr && q != end) {
          work->spend(q - p + 1);
          return false;
        }
        result = {(m.flags[state] & 2) != 0, true};
        break;
      }
      if (q == until) until = watched(q);
      if (const auto here = static_cast<TextOffset>(q - src); here % kSettledStride == 0) {
        const MonitorPlace place{monitor, state, here};
        if (const auto known = settled.find(place); known != settled.end()) {
          result = known->second;
          break;
        }
        passed.push_back(place);
      }
      state = m.next[state * m.classes + m.byteClass[byte_at(q)]];
      if (state == 0) break;
      if ((m.flags[state] & 1) != 0) {
        result.value = true;
        break;
      }
    }
    observe_end(result.atCut);
    if (work && !work->spend(q - p + (q != end))) return false;
    for (const auto& place : passed) settled.emplace(place, result);
    if (result.value == (t.waitWord[at + 1] != 0)) return false;
  }
  return true;
}

Token Machine::grouped(Token tok, std::vector<std::uint16_t> peers) {
  std::ranges::sort(peers);
  peers.erase(std::unique(peers.begin(), peers.end()), peers.end());
  if (!matchTies) matchTies = std::make_shared<EqualTokens>();
  const auto id = matchTies->groups.intern(std::move(peers));
  if (id < Token::kGroupMask) {
    tok.head |= (id + 1) << Token::kGroupShift;
  } else {
    const auto extended = matchTies->wide.intern({tok.begin(*this), id});
    if (extended > (~std::uint64_t{0} >> Token::kGroupShift))
      throw std::length_error("too many lexical ties");
    tok.head = (extended << Token::kGroupShift) | Token::kWide | static_cast<std::uint32_t>(tok.head);
  }
  return tok;
}

bool Machine::reads_tied(Token tok, std::uint16_t terminal) const {
  if (!tok.hasGroup()) return true;
  const auto group = tok.head >> Token::kGroupShift;
  const auto id = tok.head & Token::kWide ? matchTies->wide.values[group]->second
                                         : (group & Token::kGroupMask) - 1;
  return std::ranges::binary_search(*matchTies->groups.values[id], terminal);
}

std::uint32_t Machine::ending(Token tok) const {
  if (endingEnd == tok.end && endingBegin == tok.begin(*this)) return endingSet;
  std::uint32_t state = 1;
  const char* p = src + tok.begin(*this);
  while (p != tok.end && state != 0) {
    const char* const edge = tok.end - p > kWatchedBytes ? watched(p) : tok.end;
    for (; p != edge && state != 0; ++p)
      state = t.transition(state, t.byteClass[byte_at(p)]);
  }
  endingBegin = tok.begin(*this);
  endingEnd = tok.end;
  return endingSet = t.lexStates[state].accept;
}

bool Machine::tied(Token tok, std::uint32_t tie) const {
  if ((tok.head & kTold) != 0) return false;
  const std::uint32_t set = ending(tok);
  return ((t.lexTies[tie * t.tieWords + set / 64] >> (set % 64)) & 1) != 0;
}

bool Machine::reads_as(Token tok, std::uint32_t mode, std::uint16_t terminal) {
  const char* const begin = src + tok.begin(*this);
  const Token again = walk(begin, mode, 1, begin);
  return again.end == tok.end && again.terminal() == terminal && rank(again) == rank(tok);
}

bool Machine::takes_tied(Token tok, std::uint16_t terminal) {
  const TiedTerminal& as = t.tied[terminal];
  if (as.solo != kNoToken) return tok.terminal() == terminal || reads_as(tok, as.solo, terminal);
  if (!as.matched) return true;
  bool reads = reads_tied(tok, terminal);
  for (std::uint32_t k = as.ties; k != as.ties + 2u * as.n && !reads; k += 2)
    reads = reads_tied(tok, t.tiedWords[k]) && tied(tok, t.tiedWords[k + 1]);
  return reads;
}

Token Machine::read_slow(Token x, unsigned mode, bool keep, Reader lex) {
  do {
    const char* p = x.end;
    if (x.terminal() == kLexStop) {
      watch(p);
      if (p >= end) return token(static_cast<TextOffset>(end - src), end, ended);
    } else {
      x = walk(p, mode, 1, p);
      const auto trivia = t.terminalTrivia[x.terminal()];
      if (trivia == Trivia::none) return x;
      if (keep && trivia == Trivia::hidden) hide(x.begin(*this), x.end, t.terminalEntry[x.terminal()]);
      p = x.end;
    }
    x = lex(*this, p, mode);
  } while (x.terminal() >= kLexStop);
  return x;
}

void Machine::watch(const char* at) {
  mark = watched(at);
  observe_end(at == end);
}

const char* Machine::watched(const char* at) const {
  throwIfCancelled(stop);
  return end - at > kWatchedBytes ? at + kWatchedBytes : end;
}

const char* Machine::run_on(std::uint32_t run, const char* p) {
  const std::uint8_t* const in = t.runSets + run * 256u;
  const auto begin = static_cast<TextOffset>(p - src);
  std::uint64_t place = stretch_place(kNoToken, run, begin);
  if (auto it = stretches.upper_bound(place); it != stretches.begin()) {
    --it;
    if ((it->first >> 32) == (place >> 32) && begin <= it->second.end) {
      place = it->first;
      p = std::min(end, src + it->second.end);
      if (p >= mark) watch(p);
      if (p == end || in[byte_at(p)] == 0) return p;
    }
  }
  for (;;) {
    const char* const until = p < mark ? mark : p;
    p = t.runScanners[run](in, p, until);
    if (p < until || p == end) {
      if (p - src - begin >= kQuietSpan)
        stretches.insert_or_assign(place, Stretch{static_cast<TextOffset>(p - src)});
      return p;
    }
    watch(p);
  }
}

template <bool kMatching>
Token Machine::walk_from(const char* begin, std::uint32_t mode, std::uint32_t state,
                         const char* q, Held held) {
  const LexBlocks win = t.modes[mode].win;
  const std::uint8_t* const onward = t.modes[mode].onward;
  // Failed walks sort before the matcher's places and the runs: none is noted
  // where the first stretch is one of theirs.
  const bool noted = !stretches.empty() && stretches.begin()->first >> 48 < kMatchState;
  if constexpr (kMatching) {
    if (matchedOf.empty()) matchedOf.resize(t.terminalCount);
    for (const Matched& read : matchedHere) matchedOf[read.terminal][read.literal] = 0;
    matchedHere.clear();
  }
  const char* entered = q;
  // Past a state no terminal of the mode goes on from, the automaton only
  // reads for other modes.
  bool more = ((onward[state >> 3] >> (state & 7)) & 1) != 0;
  for (;;) {
    // The matcher reads the terminals whose first bytes end here; the
    // automaton's are read as they end.
    const auto word = win.at(0, t.lexStates[state].accept);
    if (word < kWinNone) {
      // Nothing read past its end: it ends further than any end before it.
      held.offered = held.quietSince = q;
      if (!held.keyword || t.terminalTrivia[word] == Trivia::none) [[likely]] {
        held.terminal = word;
        held.last = q;
        held.named = held.literal = false;
      }
    } else if (word != kWinNone) {
      LexResolution r = reading(word, q);
      if constexpr (!kMatching) {
        if (r.test == LexTest::match) [[unlikely]]
          return walk_from<true>(begin, mode, state, q, held);
      } else {
        while (r.test == LexTest::match) {
          // A walk matches each branch rank once, wherever its lead ends.
          held.offered = held.quietSince = q;
          auto& at = matchedOf[r.token][r.literal];
          if (at == 0) {
            matchedHere.push_back({r.token, r.named, r.keyword, r.literal, match(r.waits, begin), begin});
            at = static_cast<std::uint32_t>(matchedHere.size());
          }
          if (Matched& read = matchedHere[at - 1]; read.end == nullptr)
            read.unclosed = q;
          r = reading(r.otherwise, q);
        }
      }
      offer(held, r, q);
    }
    if (!more) break;
    if (q - held.quietSince >= kQuietSpan) [[unlikely]] {
      trail.push_back({state, static_cast<TextOffset>(entered - src),
                       static_cast<TextOffset>(q - src)});
      held.quietSince = q;
    }
    if (q >= mark) {
      watch(q);
      if (q == end) break;
    }
    state = t.transition(state, t.byteClass[byte_at(q)]);
    if (state == 0) break;
    ++q;
    entered = q;
    // An earlier walk went on from here and found nothing.
    if (noted) [[unlikely]] {
      const auto offset = static_cast<TextOffset>(q - src);
      const std::uint64_t place = stretch_place(mode, state, offset);
      if (auto it = stretches.upper_bound(place);
          it != stretches.begin() && (--it)->first >> 32 == place >> 32 &&
          offset <= it->second.end) {
        observe_end(ended != kEndOfInput);
        break;
      }
    }
    more = ((onward[state >> 3] >> (state & 7)) & 1) != 0;
    if (!more) continue;
    if (const std::uint8_t run = t.lexRun[state]; run != 0) {
      // A short run is read here, a long one by its scanner.
      const std::uint8_t* const in = t.runSets + (run - 1u) * 256u;
      const char* const near = end - q > kShortRun ? q + kShortRun : end;
      while (q != near && in[byte_at(q)] != 0) ++q;
      if (q == near && q != end) q = run_on(run - 1u, q);
    }
  }
  // The stretches after the last end offered gave nothing.
  if (!trail.empty()) [[unlikely]] {
    const auto from = static_cast<TextOffset>(held.offered - src);
    for (const Quiet& stretch : trail)
      if (stretch.lo > from)
        stretches.emplace(stretch_place(mode, stretch.state, stretch.lo), stretch.hi);
    trail.clear();
  }
  // Only complete matches compete by length and rank. If none succeeded,
  // the longest failed prefix supplies the span for recovery.
  std::vector<std::uint16_t> alike;
  if constexpr (kMatching) {
    const std::uint16_t walked = held.terminal;
    const char* const walkedEnd = held.last;
    const bool walkedLiteral = held.literal;
    const char* unclosed = begin;
    for (const Matched& read : matchedHere) {
      if (held.keyword && t.terminalTrivia[read.terminal] != Trivia::none) continue;
      if (read.end == nullptr) {
        unclosed = std::max(unclosed, read.unclosed);
        continue;
      }
      if (read.end == begin ||
          (read.terminal == walked && read.end == walkedEnd && read.literal == walkedLiteral))
        continue;
      const auto previous = held.terminal;
      const Offer result = offer(held, {.token = read.terminal, .keyword = read.keyword,
                                        .named = read.named, .literal = read.literal}, read.end);
      if (result == Offer::better) alike.clear();
      if (result == Offer::tied) {
        if (alike.empty()) alike.push_back(previous);
        alike.push_back(read.terminal);
      }
    }
    if (held.terminal == t.terminalCount) held.last = std::max(held.last, unclosed);
  }
  Token result = token(static_cast<TextOffset>(begin - src), held.last, held.terminal);
  if (held.literal) result.head |= kLiteralBranch;
  if constexpr (kMatching)
    if (!alike.empty()) return grouped(result, std::move(alike));
  return result;
}

template Token Machine::walk_from<false>(const char*, std::uint32_t, std::uint32_t, const char*,
                                         Held);
template Token Machine::walk_from<true>(const char*, std::uint32_t, std::uint32_t, const char*,
                                        Held);

// A record a byte at most: the first 64K records at once, then what the text
// takes at the density they show.
void reserve_for(CstRun& tape, std::size_t bytes) {
  constexpr std::size_t kFirst = 64 * 1024;
  tape.reserveRecords(std::min(kFirst, bytes + 64), bytes + 64);
}

bool parse(const Tables& tables, std::string_view text, Cst& cst,
           const CancellationToken& cancelToken) try {
  throwIfCancelled(cancelToken);
  cst.reset(text);
  text = cst.text();
  CstBuilder builder(cst);
  reserve_for(builder.stream(), text.size());
  for (bool speculate = tables.predict != nullptr;; speculate = false) {
    CstRun::Watch watch(builder.stream(), cancelToken);
    Machine machine(tables, text, builder.stream(), cancelToken);
    bool retry = false;
    const Exit exit = run(machine, speculate ? &retry : nullptr);
    if (retry) {
      builder.stream().clear();
      continue;
    }
    const bool accepted = exit == Exit::accepted || repair(machine, builder);
    builder.finish(accepted ? machine.root : kNoNode, accepted);
    return accepted;
  }
} catch (const OperationCancelled&) {
  cst.reset({});
  throw;
}

}  // namespace cursiv::engine
