// One parse under way: the text, the stack and the tape, as the generated
// code of a language drives them. Everything a token costs is inline here.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Tables.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// One symbol on the stack: the state reached, and where its records begin.
struct Frame {
  std::uint32_t state;
  NodeCount open;
};

// A token as a function hands it back, in two registers: where its text
// ends, which the next token is read from, then the rest.
struct Token {
  const char* end;
  // The low bits carry the terminal and flags. A compact tie leaves the start in the high half.
  std::uint64_t head;

  [[nodiscard]] std::uint16_t terminal() const {
    return static_cast<std::uint16_t>(head);
  }
  [[nodiscard]] TextOffset begin(const Machine& m, bool matching = true) const;
  static constexpr std::uint64_t kWide = std::uint64_t{1} << 16;
  static constexpr unsigned kGroupShift = 21;
  static constexpr unsigned kGroupMask = (1u << (32 - kGroupShift)) - 1;
  [[nodiscard]] bool hasGroup() const {
    return (head & (kWide | std::uint64_t{kGroupMask} << kGroupShift)) != 0;
  }
};

// A mixed terminal matched its literal branch.
inline constexpr std::uint64_t kLiteralBranch = std::uint64_t{1} << 17;
// A split fixed the terminal of a tie for the rest of this reading.
inline constexpr std::uint64_t kTold = std::uint64_t{1} << 18;
// The line's indentation uses a different blank at an open level.
inline constexpr std::uint64_t kMixedIndent = std::uint64_t{1} << 19;

// A repair substituted a different literal for this source token.
inline constexpr std::uint64_t kReplaced = std::uint64_t{1} << 20;

CURSIV_ALWAYS_INLINE inline Token token(TextOffset begin, const char* end,
                                        std::uint16_t terminal) {
  return {end, terminal | std::uint64_t{begin} << 32};
}

// Where a reading stands in the indentation: its innermost level, and how
// many brackets it stands in.
struct Indented {
  std::uint32_t level = 0;
  std::uint32_t depth = 0;
  friend bool operator==(const Indented&, const Indented&) = default;
};

CURSIV_ALWAYS_INLINE inline unsigned byte_at(const char* p) {
  return static_cast<unsigned char>(*p);
}

#if defined(__SSE2__) || defined(__wasm_simd128__)
using LaneWord = std::uint8_t __attribute__((vector_size(16)));
using SignedLanes = std::int8_t __attribute__((vector_size(16)));

CURSIV_ALWAYS_INLINE inline LaneWord load_lanes(const char* p) {
  LaneWord word;
  std::memcpy(&word, p, sizeof word);
  return word;
}

// Bias the interval to end at the greatest signed byte.
CURSIV_ALWAYS_INLINE inline LaneWord lanes_in(LaneWord w, unsigned lo, unsigned hi,
                                               unsigned mask = 0x7f) {
  w &= static_cast<std::uint8_t>(mask | 0x80);
  if (lo == hi) return w == static_cast<std::uint8_t>(lo);
  w += static_cast<std::uint8_t>(127 - hi);
  return std::bit_cast<SignedLanes>(w) >= static_cast<std::int8_t>(127 - (hi - lo));
}

// Index of the first flagged lane, or the full width.
CURSIV_ALWAYS_INLINE inline unsigned first_lane(LaneWord marked) {
  using Chars = char __attribute__((vector_size(16)));
#if defined(__SSE2__)
  const unsigned bits = __builtin_ia32_pmovmskb128(std::bit_cast<Chars>(marked));
#else
  const unsigned bits = __builtin_wasm_bitmask_i8x16(std::bit_cast<Chars>(marked));
#endif
  return std::countr_zero(bits | (1u << sizeof(LaneWord)));
}
#else
using LaneWord = std::uint64_t;
inline constexpr std::uint64_t kLaneOnes = 0x0101010101010101ull;
inline constexpr std::uint64_t kLaneHighs = 0x8080808080808080ull;

// Eight bytes of text as eight lanes, the first byte lowest.
CURSIV_ALWAYS_INLINE inline std::uint64_t load_lanes(const char* p) {
  std::uint64_t word;
  std::memcpy(&word, p, 8);
  if constexpr (std::endian::native == std::endian::big)
    word = __builtin_bswap64(word);
  return word;
}

// The high bit of every lane of `word` whose byte, under `mask`, is in
// `[lo, hi]`: seven-bit lanes, so no sum carries into its neighbour. A byte
// past ASCII is told apart by its own high bit, which the caller reads.
CURSIV_ALWAYS_INLINE inline std::uint64_t lanes_in(std::uint64_t word,
                                                  unsigned lo, unsigned hi,
                                                  unsigned mask = 0x7f) {
  const std::uint64_t lanes = word & (kLaneOnes * mask);
  if (lo == hi) return ~((lanes ^ (kLaneOnes * lo)) + kLaneOnes * 0x7fu);
  if (lo == 0) return ~(lanes + kLaneOnes * (0x7fu - hi));
  if (hi == 0x7f) return lanes + kLaneOnes * (0x80u - lo);
  return (lanes + kLaneOnes * (0x80u - lo)) ^ (lanes + kLaneOnes * (0x7fu - hi));
}

CURSIV_ALWAYS_INLINE inline unsigned first_lane(LaneWord marked) {
  return std::countr_zero(marked & kLaneHighs) / 8;
}
#endif

// The text read between two reads of a watched stop: about a millisecond of
// lexing.
inline constexpr std::ptrdiff_t kWatchedBytes = std::ptrdiff_t{1} << 20;

// How far the lexer's automaton reads past the last end it could give before
// what it reads is noted, and how far apart the notes stand.
inline constexpr std::ptrdiff_t kQuietSpan = 64;

// The run the lexer's automaton reads a byte at a time before it calls the
// run's scanner.
inline constexpr std::ptrdiff_t kShortRun = 16;

// The real tokens a reading takes past its last edit before a repair holds,
// or past a reading that fell before that one is let go.
inline constexpr std::uint8_t kValidateShifts = 3;

// A stretch's mode, state and start: see `Machine::stretches`.
inline constexpr std::uint32_t kUnmatched = kNoToken - 1;
inline constexpr std::uint32_t kMatchState = kNoToken - 2;
constexpr std::uint64_t stretch_place(std::uint32_t mode, std::uint32_t state, TextOffset at) {
  return std::uint64_t{mode} << 48 | std::uint64_t{state} << 32 | at;
}

// Byte work available to a lexical repair, shared with its deferred guards.
struct LexWork {
  std::uint64_t limit, read = 0;
  bool cut = false;
  bool spend(std::uint64_t n = 1) {
    read += n;
    if (read > limit) cut = true;
    return !cut;
  }
};

struct Machine {
  // Completion keeps a predicate that inspected the cut with the reading that asked it.
  using Predicate = bool (*)(Machine&, const char*, Token, Indented);
  struct CaretTests {
    virtual bool test(Predicate predicate, const char* p, Token tok, Indented indent) = 0;
    std::uint32_t chain = 0;
  };
  Machine(const Tables& tables, std::string_view text, CstRun& tape,
          const CancellationToken& stop, CaretTests* tests = nullptr)
      : t(tables),
        src(text.data()),
        end(text.data() + text.size()),
        mark(stop.stop_possible() ? src : end),
        tape(tape),
        stop(stop),
        at(text.data()),
        _caretTests(tests) {
    push(0, 0);
  }

  // A run goes on from one token to the next without coming back, and is
  // told nothing of what it derives.
  static constexpr bool kSteps = false;
  // The machine whose text the lexers read.
  CURSIV_ALWAYS_INLINE Machine& lexing() { return *this; }
  // The frames of a production popped: where its records begin.
  CURSIV_ALWAYS_INLINE NodeCount pop(std::uint32_t count, std::uint32_t = 0) {
    top -= count;
    return top->open;
  }
  // The state at a depth below the top, or none beyond the stack.
  CURSIV_ALWAYS_INLINE std::uint32_t under(std::uint32_t depth = 0) const {
    return !depth || depth < static_cast<std::size_t>(top - frames) ? (top - depth)[-1].state : ~std::uint32_t{};
  }
  // Where the records of a production that read nothing begin.
  CURSIV_ALWAYS_INLINE NodeCount here(std::uint32_t) const { return tape.records(); }
  // A run stopped after a token it shifted: it goes on from the state on
  // top, to read the next one.
  CURSIV_ALWAYS_INLINE Exit shifted() {
    resume = 2 * under();
    return Exit::shifted;
  }
  // Resume with the refused token after every shift and any pending repair.
  Exit refused(Token held) {
    tok = held;
    if (const auto records = tape.records(); records != 0)
      at = std::max(at, src + tape.at(records - 1).end);
    resume = 2 * under() + 1;
    return Exit::refused;
  }

  // A run stopped on a token its state has `count` actions for, each a
  // resume point from `first` on: it goes on from the one chosen.
  [[gnu::cold]] CURSIV_NOINLINE Exit split(Token held, std::uint32_t first, std::uint32_t count,
                                         std::uint32_t reread = 0) {
    refused(held);
    arms = first;
    armCount = count;
    rereads = reread;
    return Exit::split;
  }

  // Room for what one step of the states writes: a frame, a record, or both.
  // A step without it makes it out of line, then is taken again.
  CURSIV_ALWAYS_INLINE bool roomy(bool frame, bool record) const {
    return (!frame || top != limit) && (!record || tape.room());
  }
  [[gnu::cold]] CURSIV_NOINLINE void make_room() {
    if (top == limit) grow();
    if (!tape.room()) tape.makeRoom(tape.records() + 1);
  }
  // Whether no hidden trivia waits to be written.
  CURSIV_ALWAYS_INLINE bool flushed() const { return pending.empty(); }

  // Pushes a frame after `roomy`; derivation arguments serve graph readings.
  CURSIV_ALWAYS_INLINE void push(std::uint32_t state, NodeCount open,
                                 Token = {nullptr, 0}) {
    *top++ = {state, open};
  }

  // Lets go of the token read after `from`: the trivia kept since then is
  // read again with it, by the state that reads it next.
  void forget(const char* from) {
    const auto at = static_cast<TextOffset>(from - src);
    std::erase_if(pending, [&](const Hidden& x) {
      return x.begin >= at && x.entry != kSkippedElement;
    });
  }

  // Whether `p` begins a line.
  [[nodiscard]] bool line_start(const char* p) const {
    return p == src || p[-1] == '\n' || p[-1] == '\r';
  }
  // The token indentation makes at line start `p` for a reading at `at`: an
  // indent over the blanks, a dedent with no text, text no terminal reads
  // where the blanks match no level; none where they match its own, or end
  // the line.
  // The muted levels the line closes are let go from `at`.
  [[nodiscard]] Token indentation(const char* p, Indented& at);
  // A reading past the shift of indent `tok`, one with no text repeating the
  // level it stands in, or past a dedent.
  void indent(Indented& at, Token tok, bool muted = false);
  void dedent(Indented& at) const {
    if (at.level != 0) at.level = levels[at.level].outer;
  }

  // Hidden trivia read before a token is written when that token is.
  CURSIV_ALWAYS_INLINE void settle() {
    if (!pending.empty()) [[unlikely]]
      flush();
  }

  // A token's leaf and frame. An immediate reduction takes its opening
  // directly and pops only the frames preceding the token.
  template <std::uint32_t Popped = 0, bool kMatching = true>
  CURSIV_ALWAYS_INLINE NodeCount shift(std::uint32_t state, std::uint32_t entry,
                                  Token token) {
    const NodeCount open = tape.records();
    tape.leaf<true>(token.begin(*this, kMatching), static_cast<TextOffset>(token.end - src), entry);
    if constexpr (Popped == 0) push(state, open);
    if constexpr (Popped > 1) return pop(Popped - 1);
    return open;
  }

  // The record over what was written since `open`, `Shapes` saying what that
  // can be: nothing, and the record stands at `at` behind the trivia read
  // before it, never before a record already written; one record, which takes
  // the entry over as `fuse` composes the two; several, closed under a new
  // one. Where the record begins.
  template <unsigned Shapes, class Fuse = std::nullptr_t>
  CURSIV_ALWAYS_INLINE NodeCount close(NodeCount open, std::uint32_t entry,
                                       [[maybe_unused]] const char* at,
                                       [[maybe_unused]] Fuse&& fuse = nullptr) {
    const NodeCount records = tape.records();
    const NodeCount count = records - open;
    if constexpr ((Shapes & kRecordEmpty) != 0) {
      const auto standing = [&](NodeCount stands) {
        const auto here = static_cast<TextOffset>(at - src);
        return stands == 0 ? here : std::max(here, tape.at(stands - 1).end);
      };
      if (Shapes == kRecordEmpty) {
        const TextOffset here = standing(records);
        tape.leaf<true>(here, here, entry);
        return records;
      }
      if (count == 0) {
        if (!pending.empty()) [[unlikely]] flush();
        const NodeCount stands = tape.records();
        const TextOffset here = standing(stands);
        tape.leaf(here, here, entry);
        return stands;
      }
    }
    CstNode& last = tape.at(records - 1);
    if constexpr ((Shapes & kRecordLone) != 0) {
      if ((Shapes & kRecordSeveral) == 0 || last.subtreeSize == count) {
        last.setElement(fuse(last.element()));
        return open;
      }
    }
    tape.close<true>(tape.at(open).begin, last.end, count + 1, entry);
    return open;
  }

  // Hidden trivia read before the next token, kept as it will be written;
  // with `kSkippedElement` for entry, text a repair skipped.
  CURSIV_ALWAYS_INLINE void hide(TextOffset begin, const char* until, std::uint32_t entry) {
    const Hidden value{begin, static_cast<TextOffset>(until - src), entry};
    if (pending.size() != pending.capacity()) [[likely]]
      pending.push_back(value);
    else
      more_hidden(value);
  }

  // The text is read: the last record written is the root, and the trivia
  // after the last token follows it. The root holds the text skipped around
  // it; one that read nothing else stands at the end of the text.
  void accept();

  // Writes the hidden trivia read and the text skipped. The symbols on top
  // that wrote nothing begin after it: both stand outside the construct that
  // follows.
  void flush();
  // Doubles the stack. A popped frame is read once more after the pop, so
  // the room past the top is kept.
  void grow();
  CURSIV_ALWAYS_INLINE bool word_at(const char* p) const { return t.word_width(p, end) != 0; }
  [[nodiscard]] bool line_between(const char* from, const char* to) const {
    while (from != to) {
      const char* const edge = to - from > kWatchedBytes ? watched(from) : to;
      for (; from != edge; ++from)
        if (*from == '\n' || *from == '\r') return true;
    }
    return false;
  }

  // Whether the text from `from` up to `to` reads as monitor `monitor`
  // reads: all of it with `whole`, else some part of it.
  [[nodiscard]] bool gap_reads(std::uint32_t monitor, const char* from, const char* to,
                               bool whole) const;
  // The text read up to `to`, which reads as `ending`: the failed walks and
  // the conditions read of the text as it ended before are let go.
  void read_to(const char* to, std::uint16_t ending);
  // Whether the conditions of list `list` hold on the text from `p` on.
  [[nodiscard]] bool settles(std::uint32_t list, const char* p, LexWork* work = nullptr);
  // Of the readings of `lexWin` word `word` for terminals that end at `q`,
  // the first whose test holds: a plain terminal as one that asks nothing,
  // one the matcher reads as its caller's to read; one of `kNoToken` where
  // none holds.
  [[nodiscard]] CURSIV_ALWAYS_INLINE LexResolution reading(std::uint16_t word, const char* q,
                                                         LexWork* work = nullptr) {
    while (word != kWinNone) {
      if (word < kWinNone) return {word};
      const LexResolution& r = t.resolutions[word & ~kWinResolved];
      if (r.test == LexTest::eof) {
        if (observe_end(q == end)) return r;
      } else if (r.test == LexTest::word) {
        const char* const at = q - r.back;
        if (observe_end(at == end)) return r;
        if (end - at < 4) {
          const auto lead = static_cast<std::uint8_t>(*at);
          observe_end(t.wordBytes[lead] > 1 && end - at < std::countl_one(lead));
        }
        if (!word_at(at)) return r;
      } else if (r.test != LexTest::waits || settles(r.waits, q, work)) {
        return r;
      }
      word = r.otherwise;
    }
    return {};
  }
  // Where the matcher reads the terminal of entry `entry` from `begin` to: its
  // furthest end; null where it reads none.
  [[nodiscard]] const char* match(std::uint32_t entry, const char* begin);
  // Whether the states of a condition, from `state` on, read the text past
  // `from`, where the last token ended at indentation `indented`: all the
  // condition names, whatever follows. Its first token is `tok`, the one
  // that comes: one that reads less of its text reads none of it.
  [[nodiscard]] bool reads(std::uint32_t state, const char* from, Token tok, Indented indented);
  // The set of terminals ending together that the text of `tok` leads to.
  [[nodiscard]] std::uint32_t ending(Token tok) const;
  // Whether the text of `tok`, not told yet, is also read as the other
  // terminals of tie `tie`: all end where it ends.
  [[nodiscard]] bool tied(Token tok, std::uint32_t tie) const;
  // Whether the matcher allowed this terminal, or the reading used the DFA alone.
  [[nodiscard]] bool reads_tied(Token tok, std::uint16_t terminal) const;
  // The rank of `tok` as its text reads: a terminal ranks as a literal
  // where a branch of it that reads one literal read it.
  [[nodiscard]] std::uint8_t rank(std::uint16_t terminal, bool literal) const {
    return t.terminalRank[terminal] & ~static_cast<unsigned>(literal);
  }
  [[nodiscard]] std::uint8_t rank(Token tok) const {
    return rank(tok.terminal(), (tok.head & kLiteralBranch) != 0);
  }
  [[nodiscard]] Token as_tied(Token tok, std::uint16_t terminal, bool mixed) const {
    const bool literal = mixed && !(rank(tok) & 1);
    tok.head = (std::uint64_t{tok.begin(*this)} << 32) | terminal | kTold | (literal ? kLiteralBranch : 0);
    return tok;
  }
  // Whether the terminal's own mode reads the same span at the same rank.
  [[nodiscard]] bool reads_as(Token tok, std::uint32_t mode, std::uint16_t terminal);

  // The lexer begins no token past `mark` without reading the stop there,
  // which throws `OperationCancelled`, and moving the mark on.
  void watch(const char* at);
  // Reads the stop at `at`: how far a reader goes on before it reads it
  // again.
  [[nodiscard]] const char* watched(const char* at) const;
  // The end of the run of set `run` from `p` on, read by its scanner with
  // the stop watched on the way.
  [[nodiscard]] const char* run_on(std::uint32_t run, const char* p);
  // Resume a generated reader at a watched stop or an opening that needs the DFA.
  Token read_slow(Token x, unsigned mode, bool keep, Reader lex);
  // The longest token at `begin` among the terminals of `mode`, read by the
  // lexer's automaton from where it stands: in `state` at `q`, that state's
  // run read; state 1 at `begin` is the start. Text none of the terminals
  // reads is the terminal past the last one.
  // With `kMatching`, the matcher reads the terminals whose first bytes it
  // meets; without, the walk goes on with it there.
  template <bool kMatching = false>
  [[nodiscard]] Token walk(const char* begin, std::uint32_t mode,
                           std::uint32_t state, const char* q) {
    return walk_from<kMatching>(begin, mode, state, q,
                                {begin, t.terminalCount, begin, begin, false, false});
  }
  // What a walk holds as it goes: the token it reads so far, where an end
  // was last offered and where it has been quiet since, whether the token is
  // a keyword, and whether only tests name it.
  struct Held {
    const char* last;
    std::uint16_t terminal;
    const char* offered;
    const char* quietSince;
    bool keyword;
    bool named;
    bool literal = false;
  };
  // An end a reading read past: the token that ends furthest wins, then one
  // the parse reads over one only tests name, then the terminal of lowest
  // rank, then the one found last, which a test let through; such an end may
  // stand before an earlier one. Once a keyword is read, trivia that begins
  // with it and reads on replaces no token.
  enum class Offer { skipped, tied, better };
  CURSIV_ALWAYS_INLINE Offer offer(Held& held, const LexResolution& r, const char* q) {
    if (r.token == kNoToken || (held.keyword && t.terminalTrivia[r.token] != Trivia::none) ||
        (r.named && held.terminal != t.terminalCount &&
         t.terminalTrivia[held.terminal] != Trivia::none))
      return Offer::skipped;
    held.offered = held.quietSince = q;
    const char* const at = q - r.back;
    if (at < held.last) return Offer::skipped;
    Offer result = Offer::better;
    if (at == held.last) {
      if (held.terminal == t.terminalCount) return Offer::skipped;
      if (held.named != r.named) {
        if (r.named) return Offer::skipped;
      } else {
        const auto next = rank(r.token, r.literal), previous = rank(held.terminal, held.literal);
        if (next > previous) return Offer::skipped;
        if (next == previous) result = Offer::tied;
      }
    }
    held.literal = r.literal;
    held.named = r.named;
    held.terminal = r.token;
    held.last = at;
    held.keyword |= r.keyword;
    return result;
  }
  template <bool kMatching>
  [[nodiscard]] Token walk_from(const char* begin, std::uint32_t mode, std::uint32_t state,
                                const char* q, Held held);

  struct Hidden {
    TextOffset begin;
    TextOffset end;
    std::uint32_t entry;
  };
  // The write that grows `pending`, out of the lexers.
  [[gnu::cold]] CURSIV_NOINLINE void more_hidden(Hidden value) {
    pending.push_back(value);
  }

  // A stretch of the automaton's walk that gave no token: `state`, entered
  // at `lo` and left at `hi`.
  struct Quiet {
    std::uint32_t state;
    TextOffset lo;
    TextOffset hi;
  };

  const Tables& t;
  const char* src;
  const char* end;
  const char* mark;
  CstRun& tape;
  const CancellationToken& stop;
  // The stack: `small` until a parse nests deeper, then `wide`.
  static constexpr std::size_t kSmallStack = 64;
  Frame* frames = small;
  Frame* top = small;
  Frame* limit = small + kSmallStack;
  // Hidden trivia read and not yet written.
  std::vector<Hidden> pending;
  std::unique_ptr<Frame[]> wide;
  // Known ends by mode, state and start. Reserved modes hold failed matcher
  // calls, matcher suffixes (start means failure), and byte runs.
  struct Stretch { TextOffset end; bool atCut = false; };
  std::map<std::uint64_t, Stretch> stretches;
  bool unmatched(std::uint64_t key) {
    const auto known = stretches.find(key);
    if (known == stretches.end()) return false;
    observe_end(known->second.atCut);
    return true;
  }
  std::vector<Quiet> trail;
  // One result per terminal and branch rank in this walk, with the longest
  // failed prefix for recovery.
  struct Matched {
    std::uint16_t terminal = 0;
    bool named = false;
    bool keyword = false;
    bool literal = false;
    const char* end = nullptr;
    const char* unclosed = nullptr;
  };
  std::vector<Matched> matchedHere;
  std::vector<std::array<std::uint32_t, 2>> matchedOf;
  // Tokens name an immutable set of tied terminals. When its number exceeds
  // the flag bits, a separate entry holds the number and the starting offset.
  struct EqualTokens {
    template <class T> struct Index {
      std::map<T, std::uint64_t> ids;
      std::vector<const T*> values;
      std::uint64_t intern(T value) {
        const auto [it, fresh] = ids.try_emplace(std::move(value), values.size());
        if (fresh) values.push_back(&it->first);
        return it->second;
      }
    };
    Index<std::vector<std::uint16_t>> groups;
    Index<std::pair<TextOffset, std::uint64_t>> wide;
  };
  std::shared_ptr<EqualTokens> matchTies;
  [[nodiscard]] Token grouped(Token tok, std::vector<std::uint16_t> peers);
  // The last token span looked up by ending() and its DFA accept set.
  mutable const char* endingEnd = nullptr;
  mutable TextOffset endingBegin = 0;
  mutable std::uint32_t endingSet = 0;
  // What a condition left to read past a token answered, by its monitor,
  // the monitor's state and the place it stood at, kept every
  // `kSettledStride` bytes of the text; the places a reading passed.
  static constexpr TextOffset kSettledStride = 64;
  using MonitorPlace = std::array<std::uint32_t, 3>;
  struct PredicateResult { bool value, atCut; };
  std::map<MonitorPlace, PredicateResult> settled;
  std::vector<MonitorPlace> passed;
  NodeId root = kNoNode;
  // Where the generated states go on from, and with what.
  std::uint32_t resume = 0;
  const char* at;
  Token tok{nullptr, 0};
  // The actions a split stopped on, and whether the last reads the token
  // again.
  std::uint32_t arms = 0;
  std::uint32_t armCount = 0;
  std::uint32_t rereads = 0;
  // The terminal read where the text ends: the end of input, or for a text
  // read up to a caret one no state acts on; and where the first split
  // began whose readings such a text ended before telling apart.
  std::uint16_t ended = kEndOfInput;
  TextOffset forked = ~TextOffset{0};
 private:
  CaretTests* _caretTests;

 public:
  // A nested read or a remembered answer can depend on the current end.
  std::uint64_t endReads = 0;
  bool observe_end(bool reached) {
    if (reached) ++endReads;
    return reached;
  }
  // Evaluates against the text, including nested guards, and reports a read of the cut.
  template <class Test>
  PredicateResult on_text(Test&& test) {
    struct Restore {
      Machine& m;
      CaretTests* tests;
      ~Restore() { m._caretTests = tests; }
    } restore{*this, std::exchange(_caretTests, nullptr)};
    const auto before = endReads;
    const bool value = test();
    return {value, endReads != before};
  }
  Token observed(Token tok) {
    observe_end(tok.end == end);
    return tok;
  }
  template <class Test>
  bool check(Test predicate, const char* p, Token tok, Indented indent) {
    if (_caretTests && ended != kEndOfInput) [[unlikely]]
      return _caretTests->test(predicate, p, tok, indent);
    return predicate(*this, p, tok, indent);
  }
  // An opening belongs to its outer level. A muted level closes without a dedent.
  struct Level {
    TextOffset begin = 0;
    TextOffset length = 0;
    std::uint32_t outer = 0;
    bool muted = false;
    auto key() const { return std::array<TextOffset, 4>{begin, length, outer, muted}; }
    // The line prefix already known to close this level, by offset and length.
    std::uint64_t closesOn = ~std::uint64_t{0};
  };
  struct Levels {
    // A trial on edited text reuses the openings of its source machine.
    const Levels* base = nullptr;
    std::size_t inherited() const { return base ? base->opened.size() : 0; }
    const Level& operator[](std::uint32_t id) const {
      const auto n = inherited();
      return id <= n ? base->opened[id - 1] : opened[id - n - 1];
    }
    bool closes(std::uint32_t id, std::uint64_t line) const {
      const auto n = inherited();
      if (id > n) return opened[id - n - 1].closesOn == line;
      if (!closings) return false;
      const auto it = closings->find(id);
      return it != closings->end() && it->second == line;
    }
    void close(std::uint32_t id, std::uint64_t line) {
      const auto n = inherited();
      if (id > n) opened[id - n - 1].closesOn = line;
      else {
        if (!closings) closings = std::make_unique<Closings>();
        (*closings)[id] = line;
      }
    }
    using Closings = std::map<std::uint32_t, std::uint64_t>;
    std::unique_ptr<Closings> closings;
    std::vector<Level> opened;
    // Openings arrive in source order until a speculative reading goes back.
    std::size_t ordered = 0;
    std::map<std::array<TextOffset, 4>, std::uint32_t> rest;
  } levels;
  // Indentation of the last line inspected, after its last form feed.
  const char* blankLine = nullptr;
  std::string_view lineBlanks;
  Indented indented;
  // The actions the splits to come go on from, as the reading ahead that
  // decided them chose, and how many are taken.
  std::vector<std::uint32_t> plan;
  std::size_t planned = 0;
  // What the conditions read so far came to, kept while the text is read.
  struct Conditions;
  Conditions* conditions = nullptr;
  // The room reading ahead fills at a split, kept for the next one.
  struct Lookahead;
  Lookahead* lookahead = nullptr;
  // Reader calls, shifts and reductions, including nested predicates and lexical probes.
  std::uint64_t explored = 0;
  // The matcher's searches, kept for the next token it reads.
  struct Matchings;
  std::unique_ptr<Matchings, void (*)(Matchings*)> matchings{nullptr, nullptr};
  Frame small[kSmallStack];
  ~Machine();
};

inline TextOffset Token::begin(const Machine& m, bool matching) const {
  if (matching && (head & kWide)) [[unlikely]] return m.matchTies->wide.values[head >> kGroupShift]->first;
  return static_cast<TextOffset>(head >> 32);
}

}  // namespace cursiv::engine
