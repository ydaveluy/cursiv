// What the generator writes beside a language's code and the runtime reads:
// the lexer's automaton for the tokens the code does not read by constants,
// and what is known of each terminal.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/text/unicode/UnicodeRange.hpp"

namespace cursiv::engine {

inline constexpr std::uint16_t kNoToken = 0xFFFF;
inline constexpr std::uint32_t kNoStack = 0xFFFFFFFFu;
// As the state a shift is written for: its token ends a turn of a list.
inline constexpr std::uint32_t kInPlace = kNoStack - 1;
// The terminal that stands for the end of input.
inline constexpr std::uint16_t kEndOfInput = 0;
// No production: what a token is derived by.
inline constexpr std::uint32_t kNoProduction = 0xFFFFFFFFu;
// Some production, which one is not told.
inline constexpr std::uint32_t kSomeProduction = 0xFFFFFFFEu;

// What must hold for a terminal to have read a token: that no word goes on
// past the token's end, that the input ends where the lexer stands, or what
// the conditions of a list of `Tables::waits` say of the text from there on.
// With `match`, the automaton read the token's first bytes only: the matcher
// reads it from its first byte.
enum class LexTest : std::uint8_t { none, match, word, eof, waits };

// A `lexWin` word below `kWinNone` is the terminal read, with no test and
// nothing read past its end; one above it is `kWinResolved | index of a
// resolution`.
inline constexpr std::uint16_t kWinResolved = 0x8000;
inline constexpr std::uint16_t kWinNone = 0x7FFF;

// How the terminals ending in one lexer state read in one mode: `token`,
// with `back` bytes read past its end, where `test` holds; else what the
// `lexWin` word `otherwise` reads.
struct LexResolution {
  std::uint16_t token = kNoToken;
  std::uint16_t otherwise = kWinNone;
  std::uint8_t back = 0;
  LexTest test = LexTest::none;
  // `token` is a keyword: once read, no trivia that begins with it and reads
  // on replaces it. Only tests name `token` in the mode: it replaces no
  // trivia, and gives way to any token that ends where it does.
  bool keyword : 1 = false;
  bool named : 1 = false;
  bool literal : 1 = false;  // The literal branch of a mixed terminal.
  // The list of conditions a `waits` test reads; the entry the matcher reads
  // a `match` test's terminal from.
  std::uint16_t waits = 0;
  friend auto operator<=>(const LexResolution&, const LexResolution&) = default;
};

// A small automaton over bytes that a condition reads text with: state 0 is
// dead and state 1 the start. `flags[state]` has bit 0 where the text read
// matches, bit 1 where it does if the input ends there.
struct TextMonitor {
  const std::uint8_t* byteClass = nullptr;
  const std::uint16_t* next = nullptr;
  const std::uint8_t* flags = nullptr;
  std::uint16_t classes = 0;
};

// How a terminal stands among the tokens: read into the tree, kept beside it
// or dropped.
enum class Trivia : std::uint8_t { none, hidden, ignored };

constexpr std::uint8_t lexical_rank(Trivia trivia, bool literal) {
  return (trivia != Trivia::none ? 2 : 0) + !literal;
}

// The change a shifted terminal makes to indentation: a bracket that opens
// mutes line ends, unless it is a block.
enum class Nesting : std::uint8_t { none, indent, dedent, open, block, close };

// What the text before a shifted token may read as: whatever the lexer
// passed, nothing, or the trivia of the mode the gap names.
inline constexpr std::uint16_t kAnyGap = 0xFFFF;
inline constexpr std::uint16_t kNoGap = 0xFFFE;

// What a production can find written when it closes its record: nothing, one
// record it takes over, or several it closes under a new one.
enum : unsigned { kRecordEmpty = 1, kRecordLone = 2, kRecordSeveral = 4 };

// The tags of marks a grammar may have: each thread of the matcher holds a
// mark of each.
inline constexpr std::uint32_t kMatchTags = 4;

struct Machine;
struct Prediction {
  std::uint32_t arm = kNoStack;
  // End of the predicted token when every other reader fails to lex one.
  TextOffset soleEnd = 0;
};
// What predicts a cell's arm: the plan of arms it numbers; with `kPlanArm`,
// the arm it numbers, taken with no token read; with `kPlanRoute`, the route
// it numbers; `kNoPlan` for nothing.
inline constexpr std::uint32_t kNoPlan = ~std::uint32_t{0};
inline constexpr std::uint32_t kPlanArm = std::uint32_t{1} << 31;
inline constexpr std::uint32_t kPlanRoute = kPlanArm >> 1;

// The mode one arm reads in and, by terminal, whether its stack shifts it:
// 0 it reads it not, 1 it reads it without a known shift, 2 it shifts it.
// An arm of tied terminals names the terminal it takes the token in hand as.
struct PrefixArm {
  std::uint32_t mode;
  const std::uint8_t* shifts;
  std::uint16_t terminal = kNoToken;
};

// The states a stack ends with, top last, and the tokens read after it, each
// in its mode.
struct PrefixPath {
  std::span<const std::uint32_t> stack;
  std::span<const std::array<std::uint16_t, 2>> tokens;
};

// A plan of arms read after the stack and the text followed `path`.
struct PrefixRoute {
  std::uint32_t plan;
  const PrefixPath* path;
};

// How cells are predicted by the state `depth` frames down: the plan of that
// state among the `count` contexts from `contexts` on, else `fallback`.
struct PrefixWay {
  std::uint32_t depth;
  std::uint32_t fallback;
  std::uint32_t contexts;
  std::uint32_t count;
};

// A language's proved prefixes: per cell from `first` on, its way plus one,
// zero for none; per context of a way, its state, ascending, and its plan;
// the arms of plan `k` from `planAt[k]` to `planAt[k + 1]`.
struct PrefixTable {
  std::uint32_t first;
  std::span<const std::uint32_t> wayOf;
  const PrefixWay* ways;
  const std::uint32_t* states;
  const std::uint32_t* plans;
  const std::uint32_t* planAt;
  const PrefixArm* arms;
  const PrefixRoute* routes;
};
struct Explorer;
struct Repairer;
struct Token;

// An explorer also stops after a shift or when another reading already
// follows its continuation. A split supplies the actions to resume from.
enum class Exit : std::uint8_t { accepted, refused, shifted, split, joined };

// The token a state of `mode` reads at `p`: the first past the trivia there.
using Reader = Token (*)(Machine&, const char* p, std::uint32_t mode);
inline constexpr std::uint16_t kLexStop = 0xFFFE;
inline constexpr std::uint16_t kLexWalk = 0xFFFF;

// A reader goes on in another by a tail call, which takes a Token handed back
// in registers. Windows hands it back through memory: there a word that drops
// its token hands back `kLexOn`, and `lex` reads on from its end.
#ifdef _WIN32
inline constexpr std::uint64_t kLexOn = ~std::uint64_t{0};
#define CURSIV_READ_ON(end) return Token{end, kLexOn}
#define CURSIV_READ_WORD(word)                                                     \
  if (const Token handed = word(m, p, mode); handed.head != kLexOn) return handed; \
  else p = handed.end;                                                             \
  continue
#else
#define CURSIV_READ_ON(end) CURSIV_MUSTTAIL return lex(m, end, mode)
#define CURSIV_READ_WORD(word) CURSIV_MUSTTAIL return word(m, p, mode)
#endif

// DFA rows share identical blocks, including each row's short final block.
struct LexBlocks {
  static constexpr std::uint32_t kWidth = 16;
  const std::uint16_t* values = nullptr;
  const std::uint32_t* offsets = nullptr;

  static constexpr std::uint32_t count(std::uint32_t width) {
    return (width + kWidth - 1) / kWidth;
  }
  std::uint16_t at(std::uint32_t row, std::uint32_t i) const {
    return values[offsets[row + i / kWidth] + i % kWidth];
  }
};

// Transition row and sets of terminals that end here or remain reachable.
struct LexState {
  std::uint32_t row;
  std::uint16_t accept;
  std::uint16_t reach;
};

// A mode's reader and its shared rows: the winner for each accept set,
// and a bit per state where a terminal can end beyond it.
struct LexMode {
  Reader read;
  LexBlocks win;
  const std::uint8_t* onward;
};

// The end, at most `end`, of the bytes from `p` on that are flagged in `in`.
using RunScanner = const char* (*)(const std::uint8_t* in, const char* p,
                                   const char* end);

// How a thread of the matcher goes on from an exit: its operand is an entry,
// a mark or a scanner.
enum class MatchAction : std::uint8_t { end, call, jump, open, close, echo, ahead, unless, scan };

// An exit of the matcher's automaton: its thread goes on at entry `to`, and
// only ends with `ends`. One more than a set of `MatchTables::sets`, 0 for
// none: `first`, the bytes it can begin with; `suffix`, a byte past a call.
struct MatchExit {
  MatchAction action = MatchAction::end;
  std::uint16_t to = 0;
  std::uint32_t arg = 0;
  std::uint16_t first = 0;
  std::uint16_t suffix = 0;
  bool ends = false;
};

// An exit a matcher state takes where `guard` holds, `back` bytes before the
// place read.
struct MatchEnd {
  std::uint16_t exit;
  std::uint16_t waits;
  std::uint8_t back;
  LexTest guard;
};

// A matcher state: its row of `MatchTables::next`, `kNoRow` where no byte
// leaves it, the first of its ends, and one more than its set of `runSets`.
struct MatchState {
  std::uint32_t row;
  std::uint32_t ends;
  std::uint8_t run;
};
inline constexpr std::uint32_t kNoRow = 0xFFFFFFFFu;

// The length a scanner reads at the head of a text, 0 for none.
using MatchScan = std::size_t (*)(std::string_view);

// The matcher's automaton: state 0 is dead, and an entry names the state its
// threads start in. A set of bytes is four words.
struct MatchTables {
  const std::uint8_t* byteClass = nullptr;
  LexBlocks next;
  const MatchState* states = nullptr;
  const MatchEnd* ends = nullptr;
  const MatchExit* exits = nullptr;
  const std::uint16_t* entries = nullptr;
  const std::uint64_t* sets = nullptr;
  const MatchScan* scans = nullptr;
};

// What a row does with `terminal`: `step` of the language's steps, with its
// operand. The steps that take more read it at the cell's place in the
// language's `operands`.
struct ActionCell {
  std::uint32_t value = 0;
  std::uint16_t terminal = kNoToken;
  std::uint16_t step = 0;
};

// The cells of a language, a word each: `value`, then `terminal`, then `step`
// from the low bits up.
struct ActionCells {
  const std::uint64_t* words;
  constexpr ActionCell operator[](std::size_t at) const {
    const std::uint64_t word = words[at];
    return {
        static_cast<std::uint32_t>(word),
        static_cast<std::uint16_t>(word >> 32),
        static_cast<std::uint16_t>(word >> 48),
    };
  }
};

// A language's first steps ask of the token in hand before cell `value`
// acts: this many of them.
inline constexpr std::uint16_t kAsks = 3;

// Where a nonterminal reduced over a state leads: the state, the step that
// enters it with no token, which of the nonterminal's call sites it is, and
// the base of the state's row; `kRereads` where it reads a held token again.
struct GotoCell {
  std::uint32_t target;
  std::uint16_t enter;
  std::uint16_t site;
  std::uint32_t row;
  // A turn of a list ended: the state that only reduces it. The cell is then
  // of the list's state, `back` frames under one pushed anew: `kNoStack`
  // for `target` where the stack tells that state.
  std::uint32_t tail = 0;
  std::uint32_t back = 0;
  static constexpr std::uint16_t kRereads = 0x8000;
};

// The gotos of a language, two words each: `target`, then `enter`, then
// `site` from the low bits up; then `row`, `back` on a byte, and `tail`.
struct GotoCells {
  const std::uint64_t* words;
  constexpr GotoCell operator[](std::size_t at) const {
    const std::uint64_t word = words[2 * at];
    const std::uint64_t more = words[2 * at + 1];
    return {
        static_cast<std::uint32_t>(word),
        static_cast<std::uint16_t>(word >> 32),
        static_cast<std::uint16_t>(word >> 48),
        static_cast<std::uint32_t>(more),
        static_cast<std::uint32_t>(more >> 40),
        static_cast<std::uint32_t>(more >> 32) & 0xFF,
    };
  }
};

// The record of symbol `at` of a production takes an entry over: `row`
// fuses the two. A production gives entries to `kMostTakes` records at most.
struct Take {
  std::uint32_t at;
  std::uint32_t row;
};
inline constexpr std::size_t kMostTakes = 4;

// A reduction as its step reads it: `production` of `size` symbols to `lhs`;
// its record `records` and the row `fuse` that fuses a lone child under it,
// or with `bySite` where its records begin, one a call site.
struct Reduction {
  std::uint32_t production;
  std::uint32_t lhs;
  std::uint32_t records;
  std::uint32_t fuse;
  // With one site: its goto `to`, and where that state only reduces, the
  // cell `next` a held token takes there by step `then`.
  std::uint32_t next;
  GotoCell to;
  std::uint32_t size;
  std::uint16_t then;
  bool bySite;
  // The symbols whose record takes an entry over.
  std::uint32_t taken = 0;
  Take takes[kMostTakes] = {};
};

// The forms a reduction's step is written for.
enum : unsigned {
  // The production reads nothing.
  kReadsNothing = 1,
  // Every call site leads to one state; that state only reduces.
  kOneSite = 2,
  kThenReduces = 4,
  // A token the production's tests name goes back to the text.
  kGivesBack = 8,
  // Its goto ends a turn of a list: at every site, at some.
  kStays = 16,
  kMayStay = 32,
  // Records of its symbols take an entry over.
  kTakes = 64
};

// An item of a state's kernel as the fewest tokens that complete a stack read
// it: the nonterminal it completes (`kAccept` for a start), the nonterminal
// after its dot (`kNone` for a terminal or none), the symbols before the dot,
// the tokens it still reads and those past that nonterminal; `kNever` where no
// tokens complete it.
struct KernelCost {
  static constexpr std::uint16_t kAccept = 0xFFFF;
  static constexpr std::uint16_t kNone = 0xFFFF;
  static constexpr std::uint16_t kNever = 0xFFF;
  std::uint16_t lhs;
  std::uint16_t symbol;
  std::uint8_t dot;
  std::uint16_t rest;
  std::uint16_t after;
  [[nodiscard]] static constexpr KernelCost of(std::uint64_t v) {
    return {static_cast<std::uint16_t>(v), static_cast<std::uint16_t>(v >> 16),
            static_cast<std::uint8_t>(v >> 32), static_cast<std::uint16_t>((v >> 40) & 0xFFF),
            static_cast<std::uint16_t>(v >> 52)};
  }
  [[nodiscard]] constexpr std::uint64_t packed() const {
    return std::uint64_t{lhs} | std::uint64_t{symbol} << 16 | std::uint64_t{dot} << 32 |
           std::uint64_t{rest} << 40 | std::uint64_t{after} << 52;
  }
};

// Whether the token in hand is one several terminals read: then the run
// stops on the `count` arms from `first` on. By `kind`, tie row `test`, the
// `n` terminals of `tiedWords` from `test` on, or the matcher's rank `test`.
struct TieCheck {
  enum Kind : std::uint16_t { row, alike, grouped, ranked };
  std::uint32_t first;
  std::uint16_t count;
  std::uint16_t rereads;
  std::uint32_t test;
  Kind kind;
  std::uint16_t n;
};

// How an arm of tied terminals reads the token as a terminal: in its own
// mode `solo` if any; else, with `matched`, as the matcher read it or as one
// of its `n` ties of `tiedWords` from `ties` on: a first terminal and a row.
struct TiedTerminal {
  std::uint16_t solo;
  bool matched;
  bool mixed;
  std::uint16_t n;
  std::uint32_t ties;
};

// A state whose tested reductions may refuse the token in hand: the text is
// then read again in `mode` and dispatched in `row`, where the token is one
// of the `n` terminals of `tiedWords` from `tested` on.
struct ReadAgain {
  std::uint32_t state;
  std::uint32_t row;
  std::uint32_t tested;
  std::uint16_t n;
  std::uint16_t mode;
};

struct Tables {
  // The language's code: its states, which read a text, and the scanner of
  // each set of `runSets`.
  Exit (*drive)(Machine&) = nullptr;
  // The same states over a machine that writes no tree, and over one that
  // writes a token the text does not hold as written.
  Exit (*explore)(Explorer&) = nullptr;
  Exit (*repair)(Repairer&) = nullptr;
  const RunScanner* runScanners = nullptr;

  // The lexer's automaton. State 0 is dead and state 1 the start.
  const std::uint8_t* byteClass = nullptr;
  LexBlocks lexNext;
  const LexState* lexStates = nullptr;
  std::uint16_t transition(std::uint32_t state, unsigned cls) const {
    return lexNext.at(lexStates[state].row, cls);
  }
  // Per state, one past its set of `runSets`: the bytes that keep it in that
  // state. A set is 256 flags.
  const std::uint8_t* lexRun = nullptr;
  const std::uint8_t* runSets = nullptr;
  const LexResolution* resolutions = nullptr;
  // Per state, the offset of its counted list of successors in byte-class order.
  const std::uint32_t* lexLeadAt = nullptr;
  const std::uint16_t* lexLeads = nullptr;
  // Per state, whether it reads on in a loop: the body of a token that waits
  // for its closer, where the mode in hand ends no token there; and the bytes
  // a token with such a body can begin with, 256 flags.
  const std::uint8_t* lexBodies = nullptr;
  const std::uint8_t* lexOpeners = nullptr;
  // The grammar's word class. Per byte: 0 where no word goes on with it, 1
  // where one does, 2 where the scalar it leads is to be read; then the
  // class's scalars past ASCII, and whether it holds a byte that starts none.
  const std::uint8_t* wordBytes = nullptr;
  const unicode::Range* wordRanges = nullptr;
  std::uint32_t wordRangeCount = 0;
  bool wordInvalid = false;
  // The width of the next word character, zero outside the word class.
  CURSIV_ALWAYS_INLINE std::size_t word_width(const char* p, const char* limit) const {
    if (p == limit) return 0;
    const auto width = wordBytes[static_cast<unsigned char>(*p)];
    return width > 1 ? wide_word_width(p, limit) : width;
  }
  [[nodiscard]] std::size_t wide_word_width(const char* p, const char* limit) const;
  std::uint16_t lexClassCount = 0;

  // Per terminal: how it stands among the tokens, its rank where several end
  // at one place, the lowest winning, and the entry of its leaf where no
  // directive stands over it, `kNoElement` for none. The terminal past the
  // last stands for text no terminal reads.
  const Trivia* terminalTrivia = nullptr;
  const std::uint8_t* terminalRank = nullptr;
  const std::uint16_t* terminalEntry = nullptr;
  std::uint16_t terminalCount = 0;
  // The terminals indentation makes, 0 where the language reads none.
  std::uint16_t indentTerminal = 0;
  std::uint16_t dedentTerminal = 0;
  // The set of `terminalSets` of the trivia whose line ends end a line.
  std::uint16_t newlineIn = 0;
  // Per terminal, its place where terminals are listed for a reader: the
  // named ones, then the tokens written in the rules.
  const std::uint16_t* terminalOrder = nullptr;
  // Whether some terminal is kept beside the tree: the states then write
  // what waits before each token they shift.
  bool hides = false;
  // Sets of terminals, a set being `followWords` words; per state, the set of
  // those it has an action for.
  const std::uint64_t* terminalSets = nullptr;
  const std::uint16_t* followOf = nullptr;
  std::uint32_t followWords = 0;
  // Per state, one more than its list of `insertWord`, 0 for none: list `k`
  // is `insertWord[insertAt[k]]` up to `insertWord[insertAt[k + 1]]`, the
  // terminals a repair tries first there, in order. The others follow by
  // number.
  const std::uint32_t* insertOf = nullptr;
  const std::uint32_t* insertAt = nullptr;
  const std::uint16_t* insertWord = nullptr;
  // Whether the state's lexical choice can depend on its stack.
  const std::uint8_t* retries = nullptr;
  // Per state, the keyword it acts on where it acts on nothing else and the
  // keyword is spelled as a word; `kNoToken` for none.
  const std::uint16_t* forced = nullptr;
  // Per state, its kernel's list of items as `KernelCost`s; per nonterminal,
  // the nonterminals its derivations begin with, ascending, each with the
  // fewest tokens past it (`nonterminal | tokens << 16`): what the fewest
  // tokens that complete a stack are read from.
  const std::uint16_t* kernelOf = nullptr;
  const std::uint32_t* kernelAt = nullptr;
  const std::uint64_t* kernels = nullptr;
  const std::uint32_t* cornerAt = nullptr;
  const std::uint32_t* corners = nullptr;
  // Per state, the set of the terminals whose insertion there may be
  // required: those of its `close` set, and those it reduces on that a state
  // reached then may need.
  const std::uint16_t* mayCloseOf = nullptr;
  // Per state, the mode it reads in, `kNoToken` where it reads no token; per
  // mode, how its states read a token, null where none reads in it.
  const std::uint16_t* stateMode = nullptr;
  std::span<const LexMode> modes;
  // The bytes dropped before a token without being read as one: 256 flags.
  const std::uint8_t* skipBytes = nullptr;
  // Per mode, the mode the text of a fault of its states is read in.
  const std::uint16_t* modeFault = nullptr;
  // Per mode, the terminal that closes the section its states read in,
  // `kNoToken` where none does; null where the grammar has no section.
  const std::uint16_t* modeClose = nullptr;
  // The conditions a lexer leaves to be read once a token ends: list `k` is
  // `waitWord[waitAt[k]]` up to `waitWord[waitAt[k + 1]]`, three words a
  // condition: its monitor of `monitors`, whether it must not match, and the
  // state its monitor is in.
  const TextMonitor* monitors = nullptr;
  const std::uint32_t* waitAt = nullptr;
  const std::uint32_t* waitWord = nullptr;
  // The matcher's automaton: null where no terminal needs it.
  const MatchTables* match = nullptr;
  // Per tie of terminals that read one text where a state expects them, a
  // bit a set of terminals ending together that such a text leads to: a row
  // is `tieWords` words.
  const std::uint64_t* lexTies = nullptr;
  std::uint32_t tieWords = 0;
  // For elections, each production's size and rank << 2. Bit 1 marks left
  // recursion, bit 0 a turn over a left-recursive or unordered prefix.
  const std::uint32_t* productionSize = nullptr;
  const std::uint32_t* productionOrder = nullptr;
  // Lists begin with their length. Each block holds up to 16 productions
  // or child block offsets; word 0 gives the shift for the outermost block.
  static constexpr unsigned unitBits = 4;
  const std::uint32_t* unitWord = nullptr;
  std::uint32_t unitProduction(std::uint32_t list, std::uint32_t level) const {
    constexpr auto mask = (1u << unitBits) - 1;
    auto block = list + 1;
    for (auto shift = unitWord[0]; shift; shift -= unitBits)
      block = unitWord[block + ((level >> shift) & mask)];
    return unitWord[block + (level & mask)];
  }
  // Unit productions crossed from a state on a symbol. Nonempty rows share
  // sorted (symbol, hop list) pairs. A shifted token uses its terminal as symbol.
  const std::uint32_t* productionSymbol = nullptr;
  const std::uint32_t* derivationOf = nullptr;
  const std::uint32_t* derivationAt = nullptr;
  const std::uint64_t* derivationWord = nullptr;
  std::uint32_t derivation(std::uint32_t from, std::uint32_t production, std::uint16_t terminal) const {
    if (!derivationOf || !derivationOf[from]) return 0;
    const auto row = derivationOf[from];
    const auto symbol = production == kNoProduction ? terminal : productionSymbol[production];
    const auto* end = derivationWord + derivationAt[row];
    const auto* at = std::lower_bound(derivationWord + derivationAt[row - 1], end, std::uint64_t{symbol} << 32);
    return at != end && (*at >> 32) == symbol ? static_cast<std::uint32_t>(*at) : 0;
  }
  // Per state, what a symbol read into it over the left operand of a left
  // recursion begins: `kNoProduction` for no turn of that recursion; else
  // the turn, and the production of the turn's second symbol it begins,
  // `kNoProduction` where it is that symbol itself. `kSomeProduction` where
  // the state does not tell.
  const std::uint32_t* turnOf = nullptr;
  const std::uint32_t* turnVia = nullptr;

  // The arm a short prefix proves, kNoStack where none is proved.
  Prediction (*predict)(Machine&) = nullptr;
  // Per DFA accept set, the terminals sharing that end. Null when no end is shared.
  const std::uint16_t* lexTieEnds = nullptr;
  // Per terminal, how an arm of tied terminals reads the token as it; the
  // lists of terminals that ties and reads again name.
  const TiedTerminal* tied = nullptr;
  const std::uint16_t* tiedWords = nullptr;

  // The words of set `set` of `terminalSets`, and whether it holds `terminal`.
  [[nodiscard]] const std::uint64_t* setOf(std::uint32_t set) const {
    return terminalSets + std::size_t{set} * followWords;
  }
  [[nodiscard]] bool inSet(std::uint32_t set, std::uint16_t terminal) const {
    return ((setOf(set)[terminal / 64] >> (terminal % 64)) & 1) != 0;
  }
};

}  // namespace cursiv::engine
