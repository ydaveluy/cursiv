// One deterministic walk over bytes. Its accepting states name the terminals
// that end there or whose matcher must read on.
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "cursiv/engine/grammar/GrammarImage.hpp"
#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/engine/parser/Bits.hpp"
#include "cursiv/engine/lexer/ByteSet.hpp"
#include "cursiv/engine/grammar/Refused.hpp"
#include "cursiv/engine/parser/Tables.hpp"
#include "cursiv/utils/Descent.hpp"

namespace cursiv::engine::gen {

// One terminal ending in a state.
struct LexAccept {
  std::uint16_t term = 0;
  // Bytes the walk read past the token's end to know it ended.
  std::uint8_t back = 0;
  LexTest guard = LexTest::none;
  // The pending conditions of a waits guard or the entry of a matcher branch.
  std::uint16_t waits = 0;
  // It ends a branch of its terminal that reads one literal.
  bool literal = false;
  friend auto operator<=>(const LexAccept&, const LexAccept&) = default;
};

// Whether literal `n` reads only where no word goes on past it: a keyword
// whose spelling ends in the grammar's word class.
inline bool word_guarded(const GrammarImage& image, const ImageNode& n) {
  return (n.flags & kLitGuardWord) != 0 &&
         detail::word_final_in(n.text, image.bound.props, image.bound.complement,
                               image.bound.scalars,
                               (n.flags & kLitFolded) != 0);
}

// The matcher of the terminals the lexer's automaton reads the first bytes
// of only: an automaton of its own, whose accepts are exits, and per entry
// its start state. A matched terminal has at least one matcher branch.
struct LexMatcher {
  std::array<std::uint8_t, 256> classOf{};
  unsigned nclasses = 0;
  std::vector<std::uint16_t> next;
  std::vector<std::vector<LexAccept>> accepts;
  std::vector<MatchExit> exits;
  std::vector<std::uint16_t> entries;
  std::vector<bool> matched;
};

// A condition's automaton as the runtime reads it: `flags[state]` has bit 0
// where a match ends in the text read, bit 1 where one does if the input ends
// there, and bit 2 where the whole text read is one if the input ends there.
struct LexMonitor {
  std::array<std::uint8_t, 256> classOf{};
  unsigned nclasses = 0;
  std::vector<std::uint16_t> next;
  std::vector<std::uint8_t> flags;
};

// Distinct successors in byte-class order, shared by states with the same list.
struct LexPaths {
  std::vector<std::uint32_t> offsets;
  std::vector<std::uint16_t> targets;
  std::span<const std::uint16_t> after(std::size_t state) const {
    const auto i = offsets[state];
    return std::span(targets).subspan(i + 1, targets[i]);
  }

  // Self-looping states and the bytes from which one can be reached.
  std::vector<std::uint8_t> bodies;
  std::array<std::uint8_t, 256> openers{};
};

struct LexDfa {
  std::array<std::uint8_t, 256> classOf{};
  unsigned nclasses = 0;
  // `next[state * nclasses + class]`; state 0 is dead, state 1 the start.
  std::vector<std::uint16_t> next;
  // Per state, its entry of `acceptSets`; entry 0 is empty.
  std::vector<std::uint16_t> acceptSet;
  std::vector<std::vector<LexAccept>> acceptSets;
  // Per state, the terminals that end in it or past it.
  std::vector<Bits> reach;
  LexPaths paths;
  // Predecessors in state order, used by terminals that end a few bytes back.
  std::vector<std::vector<std::uint16_t>> predecessors;
  // What reads the terminals the automaton reads the first bytes of only.
  LexMatcher matcher;
  // The conditions left to be read once a token ends: the monitors, and the
  // lists of (monitor, whether it must not match, the monitor's state).
  std::vector<LexMonitor> monitors;
  std::vector<std::vector<std::array<std::uint32_t, 3>>> waits;

  [[nodiscard]] unsigned nstates() const {
    return static_cast<unsigned>(acceptSet.size());
  }
};

struct Terminal;

class LexAutomatonBuilder {
 public:
  explicit LexAutomatonBuilder(GrammarImage& image) : _image(image) {}

  // Adds the terminal and its matcher entry when it needs one.
  void add(std::uint16_t term, const Terminal& terminal);
  // The monitor that reads the pattern `body` of a gap test, wherever it
  // begins with `anywhere`: a place among the automaton's monitors. Throws
  // `Refused` where it is not regular.
  std::uint32_t pattern(ImageId body, bool anywhere);
  // The lexical guard the AST reads for a folded literal of a mixed terminal.
  std::uint32_t fold_guard(ImageId literal);

  // The automaton of the terminals added, `nterms` being how many the
  // grammar has.
  [[nodiscard]] LexDfa build(std::size_t nterms) const;

 private:
  struct Edge {
    ByteSet on;
    std::uint32_t to = 0;
    // Where the byte is read as a lead that starts no scalar: what the bytes
    // after it must not go on to spell, 0 otherwise.
    std::uint8_t claim = 0;
  };
  // A step that reads nothing and holds where the bytes that follow match
  // what `monitor` reads, or with `unless` where they do not.
  struct Test {
    std::uint32_t to = 0;
    std::uint32_t monitor = 0;
    bool unless = false;
  };
  struct State {
    std::vector<Edge> edges;
    std::vector<std::uint32_t> eps;
    std::vector<Test> tests;
  };
  // A test that waits for its monitor: the monitor, whether it must not
  // match, and the monitor's state.
  using Wait = std::array<std::uint32_t, 3>;
  using Waits = std::vector<Wait>;
  struct Frag {
    std::uint32_t in = 0;
    std::uint32_t out = 0;
  };
  struct Closer {
    std::string bytes;
    bool consumed = true;
  };
  struct NotRegular {
    const char* why = "";
  };

  std::uint32_t state();
  void edge(std::uint32_t from, const ByteSet& on, std::uint32_t to,
            std::uint8_t claim = 0);
  void eps(std::uint32_t from, std::uint32_t to);
  void accept(std::uint32_t at, std::uint8_t back, LexTest guard, std::uint16_t waits = 0);
  void test(std::uint32_t from, std::uint32_t to, std::uint32_t monitor, bool unless);
  // The monitor of what condition body `body` reads, read `how`; of one
  // character of the word class.
  std::uint32_t monitor(ImageId body, std::uint8_t how = kAt);
  std::uint32_t add_monitor(const LexAutomatonBuilder& reads);
  std::uint16_t wait_list(const Waits& waits);
  std::uint32_t word_monitor();
  std::uint32_t end_monitor();
  // `waits` with a test begun, or moved on by a byte, or at the end of
  // input: false where a test fails.
  [[nodiscard]] bool begun(Waits& waits, const Test& test) const;
  [[nodiscard]] bool moved(Waits& waits, unsigned byte) const;
  [[nodiscard]] bool ended(const Waits& waits) const;
  // A state where what is read ends as `accept` once no test waits.
  using End = std::pair<std::uint32_t, LexAccept>;
  // The states from `first` on, whose tests are steps the automaton cannot
  // take, as states it can: each with the tests that wait beside it. The
  // accepts from `inner` on are those the body gave of itself. The ends from
  // `stops` on are a lead's, taken where they stand: the matcher reads what
  // the tests ask.
  void expand(std::uint32_t first, const std::vector<End>& ends, std::size_t inner,
              std::size_t stops);
  // `ends` accepted: expanded where the states from `first` on hold a test.
  void finish(std::uint32_t first, const std::vector<End>& ends, std::size_t inner,
              std::size_t stops);

  // The automaton read from each NFA state of `starts`, made its own start:
  // dead state 0, the first start 1.
  [[nodiscard]] LexDfa automaton(std::vector<std::uint32_t>& starts) const;

  // Adds the first bytes of `body` as `_term`, each end of them accepted
  // with `guard`.
  bool add_lead(ImageId body, LexTest guard);
  // Starts a body: no test met, no rule being inlined.
  void begin_body();
  // Adds one regular terminal body to the automaton.
  bool add_body(std::uint16_t term, ImageId body, bool tail);
  struct Run {
    ByteSet bytes;
    std::size_t root, ends, limit;
    bool nullable, ignored;
  };
  void consider(std::size_t first, std::size_t ends, std::size_t root, bool ignored);
  void factor(const Run& run);
  // The branches of `body` that rank apart, each with whether it is one
  // literal.
  [[nodiscard]] std::vector<std::pair<ImageId, bool>> branches(ImageId body) const;
  // Features of a body that affect its matcher program or its DFA prefix.
  enum : std::uint8_t { kCalls = 1, kEchoes = 2, kMarks = 4, kScanner = 8, kLoop = 16 };
  std::uint8_t needs(ImageId id);
  // The walk of a body keeps its calls on the heap: a grammar's depth costs no
  // native stack. The matcher's own constructs are exits where `_matching`.
  detail::Descent<Frag> compile(ImageId id, bool tail);
  // The first bytes of `id`, which the automaton reads, up to the
  // constructs only the matcher reads: their fragment, whose out is where
  // reading goes on past `id`, none where only the matcher does. Where a
  // path stops, `stop` notes the state in `_leads`.
  detail::Descent<Frag> lead(ImageId id, std::uint8_t stops);
  void stop(std::uint32_t at);
  // `id` as the automaton reads it; none where it cannot, and nothing added.
  detail::Descent<std::optional<Frag>> attempt(ImageId id);
  // A condition on the text that comes, which `body` reads or, with
  // `unless`, does not.
  detail::Descent<Frag> condition(ImageId body, bool unless);
  // Exit `action` from `f.in` to `f.out`, taken where the first bytes of
  // `body` stand; with `unless`, where they do not, `f.out` is reached.
  void exit_ahead(const Frag& f, ImageId body, MatchAction action, std::uint32_t arg,
                  bool unless);
  // The exit `action` at `at`, the matcher going on from `to`.
  void exit(std::uint32_t at, MatchAction action, std::uint32_t arg, std::uint32_t to);
  // The entry the matcher reads from at NFA state `state`.
  std::uint32_t entry(std::uint32_t state);
  // The entry of the matcher's reading of `body`, which `programs` reads;
  // with `most`, of `least` to `most` copies of it.
  std::uint32_t program(ImageId body, std::uint32_t family = kOwnFamily, std::uint64_t least = 0,
                        std::uint64_t most = 0);
  // The tag of a mark or an echo.
  std::uint32_t tag(std::string_view name);
  // Reads the bodies `program` asked for, terminal `name` being added.
  void programs(std::string_view name);
  Frag literal(const ImageNode& n, bool tail);
  Frag char_class(const ImageNode& n);
  detail::Descent<Frag> parts(ImageNode n, bool tail);
  detail::Descent<Frag> choice(ImageNode n, bool tail);
  // `item` read `least` times, then up to `most` in all, `kUnbounded` for
  // no bound.
  detail::Descent<Frag> copies(ImageId item, std::uint64_t least, std::uint64_t most);
  detail::Descent<Frag> through(ImageNode n, bool tail);
  void closers(ImageId close, std::vector<Closer>& out, bool& eof) const;

  GrammarImage& _image;
  std::vector<State> _states{State{}};
  // Per accepting state, what ends there.
  std::vector<std::pair<std::uint32_t, LexAccept>> _accepts;
  // The terminal rules being inlined, and a flag per rule: one met again is
  // a recursion.
  std::vector<std::uint32_t> _inlining;
  std::vector<bool> _inlined;
  std::uint16_t _term = 0;
  bool _literal = false;
  // The monitors made, by the condition body each reads; the word class's.
  std::vector<LexMonitor> _monitors;
  // By the body read and how: from where it stands, wherever it begins, or
  // its first bytes up to what only the matcher reads.
  enum : std::uint8_t { kAt, kAnywhere, kLead };
  std::map<std::pair<ImageId, std::uint8_t>, std::uint32_t> _monitorOf;
  std::uint32_t _wordMonitor = ~std::uint32_t{0};
  std::uint32_t _endMonitor = ~std::uint32_t{0};
  // Why the last terminal refused was not regular.
  const char* _why = "";
  // Matcher exits, entries and bodies waiting to be compiled. Exit 0 ends a reading.
  bool _matching = false;
  std::vector<MatchExit> _exits{MatchExit{}};
  std::vector<End> _exitAt;
  std::vector<std::uint32_t> _entries;
  std::map<std::uint32_t, std::uint32_t> _entryOf;
  // A body read is of a family: a terminal or a call, which holds no mark of
  // its caller, with the conditions it asks, which hold its marks; the tags
  // are numbered in each family.
  static constexpr std::uint32_t kOwnFamily = ~std::uint32_t{0};
  struct ToRead {
    ImageId body = 0;
    std::uint32_t in = 0;
    std::uint32_t family = 0;
    std::uint64_t least = 0;
    std::uint64_t most = 0;
  };
  std::map<std::tuple<ImageId, std::uint32_t, std::uint64_t, std::uint64_t>, std::uint32_t>
      _programOf;
  std::vector<ToRead> _toRead;
  std::uint32_t _family = 0;
  std::map<std::pair<std::uint32_t, std::string_view>, std::uint32_t> _tags;
  std::map<std::uint32_t, std::uint32_t> _tagCount;
  // Whether the lead being added passed a condition only the matcher reads.
  bool _passed = false;
  std::vector<std::uint32_t> _leads;
  // Features per expression, or its local graph index while computing them.
  std::unordered_map<ImageId, std::uint32_t> _needs;
  // Whether a monitor is read at run time: by a gap test, or by a list of
  // conditions.
  bool _monitored = false;
  // The lists of conditions left to be read at run time, each numbered once.
  std::vector<Waits> _waits;
  std::map<Waits, std::uint32_t> _waitOf;
  // First bytes seen among all branching repetitions and among the ignored ones.
  ByteSet _loopLeads, _ignoredLeads;
  std::vector<Run> _isolated;
  // Tests in the current body, unbounded lookahead in its terminal, and monitor nesting.
  bool _tested = false;
  bool _unboundedTest = false;
  unsigned _depth = 0;
};

}  // namespace cursiv::engine::gen
