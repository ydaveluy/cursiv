#include "cursiv/engine/recovery/Repair.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Repairer.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/engine/recovery/Typos.hpp"
#include "cursiv/engine/recovery/Unclosed.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

namespace {

constexpr std::uint32_t kNoLink = 0xFFFFFFFFu;
constexpr std::uint32_t kNoReading = 0xFFFFFFFFu;
constexpr TextOffset kReachAll = 0xFFFFFFFFu;

// What an insertion or a deletion costs.
constexpr std::uint32_t kEditCost = 8;
// The bytes of a walk, or the frames of a stack, one unit of work pays for.
constexpr std::uint64_t kWorkBytes = 32;
// A stack no tokens complete.
constexpr std::uint32_t kNoCompletion = ~std::uint32_t{0};
// The work a parse may spend on its repairs: a floor and a share a byte, up
// to what bounds its latency; and the most one search takes of it.
constexpr std::uint64_t kWorkFloor = std::uint64_t{1} << 16;
constexpr std::uint64_t kWorkPerByte = 64;
constexpr std::uint64_t kWorkPerParse = std::uint64_t{1} << 21;
constexpr std::uint64_t kWorkPerSearch = std::uint64_t{1} << 19;
// The tokens a race between goals of one cost may read.
constexpr std::uint32_t kRaceSteps = 1024;
// The tokens before the one refused a search looks back over for a keyword
// misspelt, and the rank of its readings: behind the edits of that token.
constexpr unsigned kBackTokens = 8;
constexpr std::uint32_t kBackChoice = 0x40000000u;

// Where a reading stands in the fault it repairs, `(ins)* (rep | del+)?`, in
// the order a fault goes through them.
enum class Phase : std::uint8_t { beforeEdit, inserting, discardOnly };

// The work queued for a reading, or none when its attempt was refused.
enum class Work : std::uint8_t { none, read, fork, insertions, replacement };

// One edit of a path, and where it was made.
struct Edit {
  // A token inserted; the author's bytes read as a terminal, or as trivia
  // the lexer refused; the token in hand dropped; or no edit: the action
  // taken at a split the text read ahead left undecided.
  enum Kind : std::uint8_t { root, ins, rep, skip, del, arm, substitute };
  std::uint32_t parent = kNoLink;
  // Its place among the edits of its reading: the lower ranks first.
  std::uint32_t choice = 0;
  Kind kind = root;
  // The first edit of a fault.
  bool opens = false;
  // Made of a word the states read: the machine is stood before it first.
  bool word = false;
  // The terminal inserted, or the one the bytes up to `end` are read as.
  std::uint16_t term = 0;
  TextOffset end = 0;
  // The state on top of the stack it was made on.
  std::uint32_t state = 0;
  const char* at = nullptr;
  Token tok{nullptr, 0};
  std::uint32_t cost = 0;
  // The action an arm goes on from.
  std::uint32_t resume = kNoStack;

  [[nodiscard]] bool matches(const Machine& m) const {
    return m.tok.head == tok.head && m.tok.end == tok.end && m.under() == state;
  }
  // The token it writes, the same for the search and for the path written. It
  // is text of the repair's: no state gives it back to be read again. An
  // insertion holds no bytes; bytes read as a terminal are that terminal
  // whatever they spell.
  [[nodiscard]] Token written(const Machine& m) const {
    if (kind == ins) {
      Token result = token(static_cast<TextOffset>(at - m.src), at, term);
      result.head |= kReplaced;
      return result;
    }
    Token result = token(tok.begin(m), m.src + end, term);
    result.head |= kReplaced | kTold;
    if (kind == substitute && (m.t.terminalRank[term] & 1)) result.head |= kLiteralBranch;
    return result;
  }
};

// Whether the edit writes a terminal there: an insertion or a reading as one.
constexpr bool writes(const Edit& e) {
  return e.kind == Edit::ins || e.kind == Edit::rep || e.kind == Edit::substitute;
}

enum class Pin : std::uint8_t { none, invented, reread };

struct Candidate : Reading {
  // The key: what its edits cost, the tokens they invented that no state
  // needed, the author's tokens they lost, how many they are, the faults
  // they make and the tokens they invented.
  std::uint32_t cost = 0;
  std::uint32_t unneeded = 0;
  std::uint32_t lost = 0;
  std::uint32_t edits = 0;
  std::uint32_t faults = 0;
  std::uint32_t invented = 0;
  // The author's tokens it read since the search set out.
  std::uint32_t tokens = 0;
  // Where the text was read up to at the refusal it repairs: it validates
  // on the tokens past it.
  TextOffset past = 0;
  // In a run of insertions on the cheapest completion of the stack, the fewest
  // tokens that complete the stack its last one left: the next costs nothing
  // where it lowers that by one.
  std::uint64_t freeInserts = 0;
  // Real tokens read since its last edit, as far as a goal counts them.
  std::uint8_t shifts = 0;
  Phase phase = Phase::beforeEdit;
  bool held = false;
  // The token it holds is one an edit put there: invented, or the author's
  // bytes read again. An invented one shifted, whether its state needed it.
  Pin pin = Pin::none;
  bool due = false;
  bool done = false;
  // No edit since its last token: the next one opens a fault.
  bool fresh = true;
  bool gone = false;

  [[nodiscard]] bool goal() const { return done || shifts >= kValidateShifts; }
};

// Work queued at `f`, with its key, source reading and first edit rank.
struct Entry {
  std::uint32_t f, unneeded, lost, edits, faults, invented, seq, at;
  std::uint32_t rank;
  Work work;
};

// What readings cost: their edits, then the author's tokens they lost, how
// many edits and faults they make and the tokens they invented.
template <class A, class B>
std::strong_ordering cost_order(std::uint32_t costA, const A& a, std::uint32_t costB, const B& b) {
  if (costA != costB) return costA <=> costB;
  if (a.lost != b.lost) return a.lost <=> b.lost;
  if (a.edits != b.edits) return a.edits <=> b.edits;
  if (a.faults != b.faults) return a.faults <=> b.faults;
  return a.invented <=> b.invented;
}

// The key: at one cost and one loss of the author's tokens, the readings
// that invented fewer tokens no state needed first.
template <class A, class B>
std::strong_ordering key_order(std::uint32_t costA, const A& a, std::uint32_t costB, const B& b) {
  if (costA == costB && a.lost == b.lost && a.unneeded != b.unneeded)
    return a.unneeded <=> b.unneeded;
  return cost_order(costA, a, costB, b);
}

// The least key first, then the first queued.
constexpr auto kEntryAfter = [](const Entry& a, const Entry& b) {
  const auto k = key_order(a.f, a, b.f, b);
  return k != 0 ? k > 0 : a.seq > b.seq;
};

// What tells two readings apart: those of one future read on alike.
struct Future {
  std::uint64_t stack;
  std::uint64_t token;
  std::uint64_t flags;
  std::uint64_t indented;
  TextOffset past;
  bool operator==(const Future&) const = default;
};
struct FutureHash {
  std::size_t operator()(const Future& f) const {
    return static_cast<std::size_t>((f.stack * 0x9E3779B97F4A7C15ull) ^ std::rotl(f.token, 29) ^
                                    std::rotl(f.flags, 47) ^ std::rotl(f.indented, 13) ^
                                    (std::uint64_t{f.past} << 17));
  }
};

// Where two paths part: the link they share, and under it the link of each,
// none for a path that ends there; and where each first edits the text past
// it, null where it edits none.
struct Parting {
  std::uint32_t shared = kNoLink;
  std::uint32_t a = kNoLink;
  std::uint32_t b = kNoLink;
  const char* editA = nullptr;
  const char* editB = nullptr;
};

// The ends an unclosed token's readings may stop at, found one at a time:
// each line end where it spans several lines, each place that cuts no word
// where it ends with its line, else its own end.
struct Ends {
  enum Regime : std::uint8_t { none, lines, line, last };
  std::vector<TextOffset> found;
  TextOffset scanned = 0;
  Regime regime = none;
  bool done = false;
};

// The edits a repair makes with no token read between them: one note.
struct Fault {
  bool open = false;
  bool del = false;
  bool mixedIndent = false;
  // The first terminal invented, the one a misspelling is read as, and the
  // token a deletion resumes on.
  ElementId first = kNoElement;
  ElementId read = kNoElement;
  ElementId resume = kNoElement;
  // Where the text was read up to when the fault opened: an insertion's
  // caret.
  TextOffset point = 0;
  // The author's bytes the fault takes.
  TextOffset begin = 0;
  TextOffset end = 0;
  // The terminals the first written one could as cheaply have been.
  std::vector<ElementId> also;
};

// A step borrows its edit until the next search, with whether it ends its
// fault and what it adds to the cost.
struct Step {
  const Edit& edit;
  bool closes = true;
  std::uint32_t cost = 0;
};

class Repair {
 public:
  // With `backs`, a search may set the machine back before a word it read.
  Repair(Machine& machine, bool backs)
      : m(machine),
        t(machine.t),
        _budget(std::min(static_cast<std::uint64_t>(m.end - m.src) * kWorkPerByte + kWorkFloor,
                         kWorkPerParse)),
        _backs(backs),
        _marked(backs ? Marked::fresh : Marked::stale) {}

  // The token the machine was refused: the next edit is written, and the
  // machine set to go on. False where there is none.
  bool pause();
  // The machine reads on to the next token refused or to the end.
  Exit onward();
  // The faults repaired, noted on `builder`.
  void finish(CstBuilder& builder);

 private:
  [[nodiscard]] TextOffset offset(const char* p) const { return static_cast<TextOffset>(p - m.src); }
  [[nodiscard]] bool spent() const { return _work > _limit; }
  // Release trial edits and their diagnostic names once the queue passes them over.
  void discard(std::size_t first) {
    while (_readings.size() > first) {
      const auto path = static_cast<std::uint32_t>(_links.size() - 1);
      if (const auto names = _under.find(path); names != _under.end()) {
        for (const ElementId term : names->second) _named.erase(std::uint64_t{path} << 32 | term);
        _under.erase(names);
      }
      _links.pop_back();
      _readings.pop_back();
    }
  }
  // The token held is told by its whole span and by what put it in hand.
  [[nodiscard]] Future future(const Candidate& r) const {
    const std::uint64_t flags = r.shifts | static_cast<unsigned>(r.phase) << 2 |
                                unsigned{r.fresh} << 4 | unsigned{r.done} << 5 |
                                unsigned{r.held} << 6 | static_cast<unsigned>(r.pin) << 7;
    return {std::uint64_t{r.node} << 32 | offset(r.at), r.held ? r.tok.head : 0,
            std::uint64_t{r.held ? offset(r.tok.end) : 0} << 32 | flags,
            std::uint64_t{r.indented.level} << 32 | r.indented.bracket, r.past};
  }

  // A word the machine read: the bytes of one token.
  struct Word {
    TextOffset begin, end;
  };

  // Whether `reads` is refused the end of the text, which ends before `word`
  // as a token no state acts on, then in hand as `tok`: the stack is left as
  // the tokens before the word made it.
  template <class Reads>
  bool stood(Word word, const Token& tok, Reads&& reads) {
    const char* const whole = m.end;
    const std::uint16_t ended = m.ended;
    m.read_to(m.src + word.begin, t.terminalCount);
    const Exit exit = reads();
    m.read_to(whole, ended);
    return exit == Exit::refused && tok.terminal() == t.terminalCount &&
           tok.begin(m) == word.begin;
  }
  // `word` in hand as a token of no terminal: an edit of it is made on that.
  [[nodiscard]] Token unread(Word word) const {
    return token(word.begin, m.src + word.end, t.terminalCount);
  }

  // The search.
  bool back();
  void forth();
  bool stand(Word word);
  [[nodiscard]] bool misspelt(Word word);
  void misread(std::uint32_t at, std::uint32_t read);
  bool start();
  std::uint32_t run(bool& exhausted, bool& raced);
  void expand(std::uint32_t at);
  bool insertions(std::uint32_t from, std::uint32_t rank, bool required);
  bool reread(std::uint32_t from, Edit edit, std::uint32_t cost);
  void lexical(std::uint32_t at, std::uint32_t& choice);
  void replacement(std::uint32_t from, std::uint32_t rank);
  bool more_ends(Ends& ends, TextOffset p, const Unclosed& reading);
  [[nodiscard]] bool cuts(TextOffset at) const;
  Unclosed unclosed_at(std::uint32_t state, TextOffset at, bool fault = false);
  bool walk_unclosed(const ModeLexer& mode, TextOffset at, bool fault, Unclosed& out);
  [[nodiscard]] Token mode_token(std::uint16_t mode, TextOffset at);
  // How far a walk from `at` may read on what the search's share has left.
  [[nodiscard]] std::uint64_t lex_room() const {
    return (_limit > _work ? _limit - _work : 0) * kWorkBytes;
  }
  [[nodiscard]] TextOffset walk_bound(TextOffset at) const {
    return at + std::min<std::uint64_t>(lex_room(), offset(m.end) - at);
  }
  void charge(const LexWork& work) {
    _work += 1 + work.read / kWorkBytes;
    if (work.cut) _work = std::max(_work, _limit + 1);
  }
  const std::vector<Typo>& typos_at(std::uint32_t state, TextOffset at);
  [[nodiscard]] bool rereads(const Candidate& r);
  std::uint32_t child(std::uint32_t from, Edit edit);
  [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> split_at(const Candidate& r);
  void forks(std::uint32_t at);
  std::uint32_t invent(std::uint32_t from, std::uint16_t term, std::uint32_t choice);
  Work read(std::uint32_t at);
  Exit step(Candidate& r, Token* taken = nullptr);
  void admit(std::uint32_t at, Work work);
  bool merge(std::uint32_t at, std::uint32_t& kept);
  void queue(std::uint32_t f, const Candidate& r, std::uint32_t at, Work work,
             std::uint32_t rank = 0);
  [[nodiscard]] Parting part(std::uint32_t a, std::uint32_t b) const;
  std::uint32_t completion(std::uint32_t node, std::uint16_t symbol = KernelCost::kNone);
  void refused_at_end(std::uint32_t at);
  [[nodiscard]] bool before(const Parting& p) const;
  void tie(std::uint32_t kept, std::uint32_t dropped, const Parting& p, bool handed);
  void own(std::uint32_t from, std::uint32_t shared, std::vector<ElementId>& out) const;
  void add_name(std::uint32_t path, ElementId term);
  void name(std::uint32_t path);
  std::uint32_t elect(bool& raced);
  bool race(std::vector<TextOffset>& reach);
  [[nodiscard]] TextOffset deleted_end(std::uint32_t state, Token tok);
  // A reading past an indent dropped, at a level that closes with no
  // dedent, or past a dedent dropped.
  void indentation_dropped(Indented& at, Token tok) {
    if (tok.terminal() == t.indentTerminal && t.indentTerminal != 0)
      m.indent(at, tok, true);
    else if (tok.terminal() == t.dedentTerminal && t.dedentTerminal != 0)
      m.dedent(at);
  }
  template <class Visit>
  void each_insert(std::uint32_t state, Visit&& visit) const;

  // The elected path, written a step a pause.
  void plan_path(std::uint32_t reading);
  bool write(const Step& st);
  [[nodiscard]] bool arm_here() const;
  std::uint32_t split_arm();
  Exit shift(bool settles, bool repairs);
  bool write_token(Token token, bool settles = false);
  bool write_insert(const Edit& edit);
  void skip(const Edit& edit);
  bool take(std::uint32_t elected, bool bound);
  bool apply();
  bool degrade();
  bool closers();
  void note();
  void open_span(TextOffset from, std::uint64_t least);
  void note_span();

  Machine& m;
  const Tables& t;

  // The work the parse may spend on repairs, what it spent, and where the
  // search under way stops. Spent, every later fault takes the first edit
  // that holds.
  std::uint64_t _budget;
  std::uint64_t _work = 0;
  std::uint64_t _limit = 0;
  bool _degraded = false;

  // The machine as it last set out to read after an edit, where a search
  // sets it back: stale once an edit is written, back while a search has it
  // stand before a word it read, used once one did.
  enum class Marked : std::uint8_t { stale, fresh, back, used };
  const bool _backs;
  Marked _marked;
  Machine::Checkpoint _mark;
  // For a reading that made no edit: how it stood before each word it read
  // that misspells a literal, with the count of tokens read before it.
  struct Looked {
    Candidate before;
    Word word;
    std::uint32_t index;
  };
  std::vector<Looked> _looked;
  // What the machine was refused when it was set back: the token, where it
  // stood in the indentation and the states under it.
  Token _refused{nullptr, 0};
  Indented _refusedIn{};
  std::vector<std::uint32_t> _refusedOn;
  // The terminals read as literals, and per fault mode and spelling whether
  // a word misspells one.
  std::vector<std::uint64_t> _literals;
  std::map<std::pair<std::uint16_t, std::string_view>, bool> _misspelt;

  // Where the end-of-text insertions were last made, and the fewest tokens
  // that then completed the stack.
  const char* _closedAt = nullptr;
  std::uint32_t _closedLeast = 0;
  // Per node, and per node with a nonterminal read on it, the fewest tokens
  // that complete its stack; and the values a computation still waits for.
  std::unordered_map<std::uint64_t, std::uint32_t> _completions;
  std::vector<std::pair<std::uint32_t, std::uint16_t>> _asked;

  // One search: the stacks, the readings, the paths and the queue.
  Cactus _stacks;
  // The states a reading's search pushed, and the actions a split it met is
  // decided with.
  std::vector<std::uint32_t> _above;
  std::vector<std::uint32_t> _arms;
  // Each candidate owns the edit at the same index.
  std::vector<Candidate> _readings;
  std::vector<Edit> _links;
  std::vector<Entry> _heap;
  std::uint32_t _seq = 0;
  std::unordered_map<Future, std::uint32_t, FutureHash> _seen;
  // The goals of the least cost.
  std::vector<std::uint32_t> _goals;
  // Per link, the first terminals written by the paths dropped for one as
  // cheap that goes through it; and what the elected path's first could as
  // cheaply have been.
  std::unordered_map<std::uint32_t, std::vector<ElementId>> _under;
  std::vector<ElementId> _also;
  std::unordered_set<std::uint64_t> _named;
  // Per mode and place, the misspellings the text there reads as; and those
  // of a walk the search's share cut, which are not all of them.
  std::unordered_map<std::uint64_t, std::vector<Typo>> _typos;
  std::vector<Typo> _cutTypos;
  // Per mode and place, the unclosed token the lexer was refused there. Per
  // place, the walks read there, each with its mode and the lexer states it
  // asked about: a mode that reads alike in them takes it over. Per place
  // and end, the ends found for an unclosed token; per mode and place, the
  // token read; per mode and place its own reads no token at, where a
  // deletion ends.
  struct Walk {
    std::uint16_t mode;
    Unclosed found;
    std::uint32_t first;
    std::uint32_t count;
  };
  std::unordered_map<std::uint64_t, Unclosed> _unclosed;
  std::unordered_map<std::uint64_t, std::vector<Walk>> _walks;
  std::vector<std::uint16_t> _walked;
  LexVisits _visits;
  std::unordered_map<std::uint64_t, Ends> _ends;
  std::unordered_map<std::uint64_t, Token> _modeTokens;
  std::unordered_map<std::uint64_t, TextOffset> _unread;

  std::vector<Step> _plan;
  // What the edits written cost, and what they did when the plan was made.
  std::uint32_t _cost = 0;
  std::uint32_t _costBase = 0;
  Fault _fault;
  std::vector<ParseDiagnostic> _notes;
  // Where a path that stopped inside a fault left the text, and where the
  // last edit made without a search did.
  const char* _openAt = nullptr;
  const char* _degradedAt = nullptr;
  // A span a work bound decided, said once its path is written and the
  // edits cost `_spanLeast` at least.
  bool _spanOpen = false;
  TextOffset _spanBegin = 0;
  TextOffset _spanEnd = 0;
  std::uint64_t _spanLeast = ~std::uint64_t{0};
};

void Repair::queue(std::uint32_t f, const Candidate& r, std::uint32_t at, Work work,
                   std::uint32_t rank) {
  const bool deferred = work == Work::insertions || work == Work::replacement;
  const bool opens = r.fresh && (deferred || (work == Work::read && !r.goal()));
  _heap.push_back({f, r.unneeded, r.lost, r.edits + (deferred ? 1u : 0u),
                   r.faults + (opens ? 1u : 0u), r.invented + (work == Work::insertions ? 1u : 0u),
                   _seq++, at, rank, work});
  std::ranges::push_heap(_heap, kEntryAfter);
}

// The fewest tokens that complete the stack of `node`, with nonterminal
// `symbol` read on it unless it is `kNone`: through the item of its kernel
// that completes cheapest, each item's own nonterminal then read on the stack
// it began on, down to the start. Each value is read once a search.
std::uint32_t Repair::completion(std::uint32_t node, std::uint16_t symbol) {
  const auto key = [](std::uint32_t n, std::uint16_t s) { return std::uint64_t{n} << 16 | s; };
  if (const auto hit = _completions.find(key(node, symbol)); hit != _completions.end())
    return hit->second;
  _asked.assign(1, {node, symbol});
  while (!_asked.empty()) {
    const auto [at, read] = _asked.back();
    if (_completions.contains(key(at, read))) {
      _asked.pop_back();
      continue;
    }
    const std::uint32_t state = _stacks.state(at);
    std::uint32_t least = kNoCompletion;
    bool known = true;
    const std::uint16_t list = t.kernelOf[state];
    for (std::uint32_t k = t.kernelAt[list]; k != t.kernelAt[list + 1]; ++k) {
      const KernelCost item = KernelCost::of(t.kernels[k]);
      // The tokens the item reads before its nonterminal is whole.
      std::uint32_t own = item.rest;
      if (read != KernelCost::kNone) {
        if (item.symbol == KernelCost::kNone) continue;
        own = item.after;
        if (read != item.symbol) {
          const std::uint32_t* first = t.corners + t.cornerAt[item.symbol];
          const std::uint32_t* last = t.corners + t.cornerAt[item.symbol + 1];
          const std::uint32_t* corner = std::lower_bound(
              first, last, read, [](std::uint32_t c, std::uint16_t s) { return (c & 0xFFFF) < s; });
          if (corner == last || (*corner & 0xFFFF) != read) continue;
          own += *corner >> 16;
        }
      }
      if (item.rest >= KernelCost::kNever || own >= KernelCost::kNever) continue;
      std::uint32_t below = 0;
      if (item.lhs != KernelCost::kAccept) {
        std::uint32_t start = at;
        for (std::uint32_t d = 0; d != item.dot && start != kNoStack; ++d) start = _stacks.parent(start);
        if (start == kNoStack) continue;
        const auto hit = _completions.find(key(start, item.lhs));
        if (hit == _completions.end()) {
          _asked.emplace_back(start, item.lhs);
          known = false;
          continue;
        }
        below = hit->second;
      }
      if (below != kNoCompletion) least = std::min(least, own + below);
    }
    if (!known) continue;
    _completions.emplace(key(at, read), least);
    ++_work;
    _asked.pop_back();
  }
  return _completions.at(key(node, symbol));
}

// A reading that reaches the end of the text validates only once accepted,
// whatever it read before: where the end refuses it right past its tokens, it
// stands there and closes what it opened, as the readings it races do.
void Repair::refused_at_end(std::uint32_t at) {
  Candidate ahead = _readings[at];
  if (step(ahead) != Exit::refused || ahead.tok.terminal() != kEndOfInput) return;
  ahead.shifts = 0;
  _readings[at] = ahead;
}

// Where paths `a` and `b` part, read from the root down.
Parting Repair::part(std::uint32_t a, std::uint32_t b) const {
  std::uint32_t x = a, y = b;
  while (x != y) {
    x = x == kNoLink ? b : _links[x].parent;
    y = y == kNoLink ? a : _links[y].parent;
  }
  Parting p{x};
  for (; a != x; a = _links[a].parent) {
    p.a = a;
    if (_links[a].kind != Edit::arm) p.editA = _links[a].at;
  }
  for (; b != x; b = _links[b].parent) {
    p.b = b;
    if (_links[b].kind != Edit::arm) p.editB = _links[b].at;
  }
  return p;
}

// Whether the first path of `p` ranks before the second: the one that edits
// the text later, keeping more of it as written; then the lower choice where
// they part, and a path that ends there first.
bool Repair::before(const Parting& p) const {
  if (p.a == kNoLink || p.b == kNoLink) return p.a == kNoLink && p.b != kNoLink;
  if (p.editA != p.editB) return p.editA == nullptr || (p.editB != nullptr && p.editA > p.editB);
  return _links[p.a].choice != _links[p.b].choice ? _links[p.a].choice < _links[p.b].choice
                                                  : p.a < p.b;
}

// What the links from `from` up to `shared` hold.
void Repair::own(std::uint32_t from, std::uint32_t shared, std::vector<ElementId>& out) const {
  for (std::uint32_t l = from; l != shared; l = _links[l].parent)
    if (const auto held = _under.find(l); held != _under.end())
      out.insert(out.end(), held->second.begin(), held->second.end());
}

// Equal paths keep their shared names and carry over those on their separate links.
void Repair::tie(std::uint32_t kept, std::uint32_t dropped, const Parting& p, bool handed) {
  if (p.shared == kNoLink) return;
  std::vector<ElementId> moved;
  own(dropped, p.shared, moved);
  if (handed) own(kept, p.shared, moved);
  for (const ElementId term : moved) add_name(kept, term);
  _work += moved.size();
}

void Repair::add_name(std::uint32_t path, ElementId term) {
  if (term != kNoElement && _named.insert(std::uint64_t{path} << 32 | term).second)
    (path == kNoLink ? _also : _under[path]).push_back(term);
}

// Collect the path's first written terminal and the alternatives its links hold.
void Repair::name(std::uint32_t path) {
  for (std::uint32_t l = path; _links[l].parent != kNoLink; l = _links[l].parent)
    if (const auto held = _under.find(l); held != _under.end())
      for (const ElementId term : held->second) add_name(kNoLink, term);
}

// Whether reading `at` dies to `kept`, the one its future keeps: at a worse
// key and no more free insertions, or at an equal one, where the kept one
// takes the better path. One that outdoes it is kept instead.
bool Repair::merge(std::uint32_t at, std::uint32_t& kept) {
  Candidate& r = _readings[at];
  Candidate& x = _readings[kept];
  const auto k = x.gone ? std::strong_ordering::less : key_order(r.cost, r, x.cost, x);
  const bool alike = !x.gone && cost_order(r.cost, r, x.cost, x) == 0;
  if (k > 0 ? r.freeInserts <= x.freeInserts : k == 0 && r.freeInserts == x.freeInserts) {
    if (alike) {
      const Parting p = part(at, kept);
      const bool handed = k == 0 && before(p);
      tie(kept, at, p, handed);
      if (handed) _links[kept] = _links[at];
    }
    r.gone = true;
    return true;
  }
  if (x.gone || (k < 0 && x.freeInserts <= r.freeInserts)) {
    if (alike) tie(at, kept, part(at, kept), false);
    x.gone = true;
    kept = at;
  }
  return false;
}

// Candidate `at` queued, unless the one its future keeps outdoes it. A refused
// reading costs at least a re-reading more; one at a split does not.
void Repair::admit(std::uint32_t at, Work work) {
  if (work != Work::none) {
    const Candidate& r = _readings[at];
    const auto [it, fresh] = _seen.try_emplace(future(r), at);
    if (fresh || !merge(at, it->second)) {
      const auto more = work == Work::read && !r.goal() && r.freeInserts == 0 ? kRereadCost : 0;
      queue(r.cost + more, r, at, work);
      return;
    }
  }
  // The readings made of the words it read stand without it.
  if (at + 1 == _readings.size()) discard(at);
}

// The reading of `from` with `term` put in hand before its token.
std::uint32_t Repair::invent(std::uint32_t from, std::uint16_t term, std::uint32_t choice) {
  const std::uint32_t e = child(from, {.choice = choice, .kind = Edit::ins, .term = term});
  Candidate& r = _readings[e];
  r.tok = _links[e].written(m);
  r.held = true;
  r.pin = Pin::invented;
  r.invented += 1;
  r.phase = Phase::inserting;
  return e;
}

std::uint32_t Repair::child(std::uint32_t from, Edit edit) {
  const auto at = static_cast<std::uint32_t>(_links.size());
  Candidate& r = _readings.emplace_back(_readings[from]);
  edit.parent = from;
  edit.state = _stacks.state(r.node);
  edit.at = r.at;
  edit.tok = r.tok;
  edit.cost = r.cost;
  if (edit.kind == Edit::arm) {
    r.resume = edit.resume;
  } else {
    edit.opens = r.fresh;
    if (r.edits == 0 && writes(edit)) add_name(at, edit.term);
    r.faults += r.fresh;
    ++r.edits;
    r.fresh = r.due = false;
    r.shifts = 0;
  }
  _links.push_back(edit);
  r.gone = false;
  ++_work;
  return at;
}

// One token of `r`: shifted, which `taken` is then set to, refused in the
// state on top, the text's end, or a split the text read ahead left
// undecided, which `r` then stands at.
Exit Repair::step(Candidate& r, Token* taken) {
  const std::uint32_t from = r.node;
  const auto before = m.explored;
  Explorer x(m, _stacks, r);
  Exit exit = x.step();
  std::size_t planned = 0;
  while (exit == Exit::split) {
    // The text ahead decides, read from the reading's stack: the states the
    // search pushed over the machine's frames; its plan serves the splits of
    // the same token it met.
    if (planned == _arms.size() || _arms[planned] < x.arms ||
        _arms[planned] >= x.arms + x.armCount) {
      _above.clear();
      std::uint32_t n = x.node;
      for (; n != kNoStack && n >= _stacks.base(); n = _stacks.parent(n))
        _above.push_back(_stacks.state(n));
      std::ranges::reverse(_above);
      _arms.clear();
      planned = 0;
      const auto stack = _stacks.frames(n);
      resolve(m, stack, _above, x.at, x.tok, x.indented, x.arms, x.armCount, x.rereads, _arms);
      _work += (stack.size() + _above.size()) / kWorkBytes;
      if (_arms.empty()) break;
    }
    x.resume = _arms[planned++];
    exit = x.step();
  }
  _arms.clear();
  _work += m.explored - before;
  // A reread that dropped the substitute cannot validate its shift.
  if (r.pin == Pin::reread &&
      (exit == Exit::accepted || !((exit == Exit::shifted ? x.taken.head : x.tok.head) & kReplaced)))
    return Exit::refused;
  r.reading() = x.reading();
  r.held = exit == Exit::refused || exit == Exit::split;
  if (r.held) return exit;
  if (taken != nullptr) *taken = x.taken;
  if (exit == Exit::accepted) {
    r.done = true;
  } else if (r.pin == Pin::invented) {
    // An invented token is no token of the author's: the fault goes on. It is
    // required where it begins the cheapest completion of the stack, or of
    // the stack the run's last insertion left; past the first of a run, such
    // a one costs nothing.
    r.pin = Pin::none;
    const std::uint32_t rest = completion(r.node);
    const std::uint64_t before = r.freeInserts != 0 ? r.freeInserts : completion(from);
    r.due = rest != kNoCompletion && rest + 1 == before;
    r.unneeded += r.due ? 0u : 1u;
    const bool goesOn = r.due && r.freeInserts != 0;
    r.cost += goesOn ? 0 : kEditCost;
    r.edits -= goesOn ? 1u : 0u;
    r.freeInserts = r.due ? rest : 0;
  } else {
    // The author's bytes read again are a token of theirs, but none past the
    // edit that read them; a reading validates past an edit only, on tokens
    // past the refusal, and indentation, made of blanks, validates nothing.
    const std::uint16_t taken = x.taken.terminal();
    if (r.pin == Pin::none && r.edits != 0 && r.shifts < kValidateShifts &&
        taken != t.indentTerminal && taken != t.dedentTerminal && x.taken.begin(m) >= r.past)
      ++r.shifts;
    r.pin = Pin::none;
    ++r.tokens;
    r.fresh = true;
    r.phase = Phase::beforeEdit;
    r.freeInserts = 0;
  }
  return exit;
}

// Reads on to a refusal, a split or a goal; none when its edit was refused
// or the search's share ran out.
Work Repair::read(std::uint32_t at) {
  // A keyword misspelt may read as a name, and be refused a token later: a
  // reading that made no edit keeps how it stood before such words.
  const bool plain = _backs && _readings[at].edits == 0;
  if (plain) _looked.clear();
  Candidate before;
  Token taken{nullptr, 0};
  for (std::uint32_t read = 0;; ++read) {
    Candidate& r = _readings[at];
    if (r.done || (r.pin == Pin::none && r.goal())) return Work::read;
    if (spent()) return Work::none;
    if (plain) before = r;
    const Exit exit = step(r, &taken);
    if (exit == Exit::refused && r.pin != Pin::none) return Work::none;
    const Token tok = r.held ? r.tok : taken;
    // A shift reads its token and a stop holds it; an acceptance reads none.
    if (plain && !before.held && (r.held || exit == Exit::shifted) &&
        tok.terminal() < t.terminalCount) {
      if (const Word word{tok.begin(m), offset(tok.end)}; misspelt(word)) {
        if (_looked.size() > kBackTokens) _looked.erase(_looked.begin());
        _looked.push_back({before, word, read});
      }
    }
    if (!r.held) continue;
    if (plain) misread(at, read);
    return exit == Exit::refused ? Work::read : Work::fork;
  }
}

// The last words reading `at` read before it stopped, `read` tokens on, and
// the word it stopped on: each read as the literals it misspells, by the
// reading stood before it.
void Repair::misread(std::uint32_t at, std::uint32_t read) {
  const Candidate stopped = _readings[at];
  std::uint32_t choice = kBackChoice;
  for (Looked& looked : _looked) {
    if (spent()) break;
    if (looked.index + kBackTokens < read) continue;
    const Word word = looked.word;
    Candidate& before = looked.before;
    if (!stood(word, before.tok, [&] { return step(before); })) continue;
    const ModeLexer mode(m, _stacks.state(before.node));
    std::vector<std::uint64_t> want = _literals;
    for (std::uint32_t w = 0; w < t.followWords; ++w) want[w] &= mode.follow[w];
    LexWork work{lex_room()};
    const std::vector<Typo> found = typos(mode, std::move(want), word.begin, word.end, work);
    charge(work);
    before.tok = unread(word);
    before.past = offset(stopped.at);
    _readings[at] = before;
    for (const Typo& typo : found) {
      if (spent()) break;
      if (typo.end == word.end)
        reread(at, {.choice = choice++, .kind = Edit::rep, .word = true, .term = typo.terminal,
                    .end = typo.end}, typo.cost);
    }
  }
  _readings[at] = stopped;
}

// The actions of the split `r` stands at, from the first, and how many; none
// where its state reads its token alone.
std::pair<std::uint32_t, std::uint32_t> Repair::split_at(const Candidate& r) {
  Explorer x(m, _stacks, r);
  x.resume = 2 * _stacks.state(r.node) + 1;
  const auto before = m.explored;
  const auto exit = x.step();
  _work += m.explored - before;
  if (exit != Exit::split) return {0, 0};
  return {x.arms, x.armCount};
}

// Each action of the split reading `at` stands at read on, in the order the
// split lists them.
void Repair::forks(std::uint32_t at) {
  const auto [first, count] = split_at(_readings[at]);
  for (std::uint32_t k = 0; k < count && !spent(); ++k) {
    const std::uint32_t e = child(at, {.choice = k, .kind = Edit::arm, .resume = first + k});
    admit(e, read(e));
  }
}

// The terminals a state offers a repair to insert, each with its rank: in the
// order of its list where it has one, else by number.
template <class Visit>
void Repair::each_insert(std::uint32_t state, Visit&& visit) const {
  std::uint32_t rank = 0;
  if (const std::uint32_t list = t.insertOf[state]; list != 0) {
    for (std::uint32_t at = t.insertAt[list - 1]; at != t.insertAt[list]; ++at)
      if (t.insertWord[at] != kEndOfInput && !visit(t.insertWord[at], rank++)) return;
    return;
  }
  for (std::uint32_t w = 0; w < t.followWords; ++w)
    for (std::uint64_t bits = t.setOf(t.followOf[state])[w]; bits != 0;
         bits &= bits - 1) {
      const auto term = static_cast<std::uint16_t>(w * 64 + std::countr_zero(bits));
      if (term != kEndOfInput && !visit(term, rank++)) return;
    }
}

// The unclosed token the mode of `state` was refused at `at`, as the state
// names it, read once a search per mode.
Unclosed Repair::unclosed_at(std::uint32_t state, TextOffset at, bool fault) {
  if (m.src + at >= m.end || t.lexOpeners[byte_at(m.src + at)] == 0 || spent()) return {};
  const ModeLexer mode(m, state, fault);
  const auto key = std::uint64_t{mode.mode} << 33 | std::uint64_t{fault} << 32 | at;
  auto hit = _unclosed.find(key);
  if (hit == _unclosed.end()) {
    Unclosed found;
    if (!walk_unclosed(mode, at, fault, found)) return {};
    if (!fault && !found.closed && t.modeClose != nullptr) {
      const auto close = t.modeClose[mode.mode];
      for (TextOffset x = at + 1; close != kNoToken && x < found.end; ++x) {
        if ((x - at - 1) % kWorkBytes == 0) {
          if (++_work > _limit) return {};
          if (m.src + x >= m.mark) m.watch(m.src + x);
        }
        const auto first = t.transition(1, t.byteClass[byte_at(m.src + x)]);
        if (first == 0 || !t.inSet(t.lexStates[first].reach, close)) continue;
        if (mode_token(mode.mode, x).terminal() == close) {
          found.end = x;
          break;
        }
        if (spent()) return {};
      }
    }
    hit = _unclosed.emplace(key, found).first;
  }
  Unclosed found = hit->second;
  if (found.body != 0) found.terminal = mode.leads_to(found.body);
  return found;
}

// The walk of `mode` at `at`: one a mode that reads alike in the states it
// asked about read, or one read now and counted as work. False where the
// search's share ran out.
bool Repair::walk_unclosed(const ModeLexer& mode, TextOffset at, bool fault, Unclosed& out) {
  std::vector<Walk>& walks = _walks[std::uint64_t{fault} << 32 | at];
  for (const Walk& w : walks) {
    const auto states = std::span(_walked).subspan(w.first, w.count);
    if (std::ranges::all_of(states, [&](std::uint16_t s) { return mode.alike(w.mode, s); })) {
      out = w.found;
      return true;
    }
  }
  LexWork work{lex_room()};
  out = unclosed(mode, at, work, _visits);
  charge(work);
  if (!work.cut) {
    walks.push_back({mode.mode, out, static_cast<std::uint32_t>(_walked.size()),
                     static_cast<std::uint32_t>(_visits.states.size())});
    _walked.insert(_walked.end(), _visits.states.begin(), _visits.states.end());
  }
  _visits.clear();
  return !work.cut;
}

// The longest token of this mode. A walk that reads past its winner pays
// for its lookahead too: double the window only when the lexer reaches it.
Token Repair::mode_token(std::uint16_t mode, TextOffset at) {
  const auto key = std::uint64_t{mode} << 32 | at;
  if (const auto hit = _modeTokens.find(key); hit != _modeTokens.end()) return hit->second;
  const auto remaining = offset(m.end) - at;
  Token result = token(at, m.src + at, t.terminalCount);
  for (std::uint64_t window = kWorkBytes;; window *= 2) {
    const auto width = static_cast<TextOffset>(std::min<std::uint64_t>({window, remaining, lex_room()}));
    if (width == 0 && remaining != 0) break;
    bool complete = width == remaining;
    if (complete) {
      result = m.walk(m.src + at, mode, 1, m.src + at);
    } else {
      Machine probe(t, {m.src, at + width}, m.tape, m.stop);
      probe.matchTies = m.matchTies;
      probe.ended = t.terminalCount;
      const auto observed = probe.on_text([&] {
        result = probe.walk(m.src + at, mode, 1, m.src + at);
        return true;
      });
      complete = !observed.atCut && result.end != probe.end;
      if (complete && result.hasGroup()) m.matchTies = std::move(probe.matchTies);
    }
    _work += std::max<std::uint64_t>(1, (width + kWorkBytes - 1) / kWorkBytes);
    if (complete) {
      _modeTokens.emplace(key, result);
      return result;
    }
    if (lex_room() <= width) break;
  }
  _work = std::max(_work, _limit + 1);
  return token(at, m.src + at, t.terminalCount);
}

// Where the deletion of `tok` ends: its own end, where an indentation
// token begins, or, for text the mode of `state` reads no token at, the end
// of the unclosed token there or its opener, of what the fault mode reads,
// else of the text up to a blank, a line end or the next token.
TextOffset Repair::deleted_end(std::uint32_t state, Token tok) {
  const TextOffset begin = tok.begin(m);
  if (tok.terminal() != t.terminalCount) return offset(tok.end);
  const auto key = std::uint64_t{state} << 32 | begin;
  if (const auto hit = _unread.find(key); hit != _unread.end()) return hit->second;
  const ModeLexer fault(m, state, true);
  TextOffset end = begin + 1;
  if (const auto& own = unclosed_at(state, begin); own.terminal != kNoToken)
    end = unclosed_cut(begin, own);
  else if (const Token whole = mode_token(fault.mode, begin);
           whole.terminal() != t.terminalCount && offset(whole.end) > begin)
    end = offset(whole.end);
  else if (const auto& open = unclosed_at(state, begin, true); open.terminal != kNoToken)
    end = unclosed_cut(begin, open);
  else {
    const auto bound = walk_bound(begin);
    for (; end < bound && !spent(); ++end) {
      const auto b = byte_at(m.src + end);
      if (b == '\n' || b == '\r' || t.skipBytes[b]) break;
      if (fault.step(1, b) || fault.accepts(1)) {
        const Token next = mode_token(fault.mode, end);
        if ((next.terminal() != t.terminalCount && offset(next.end) > end) ||
            unclosed_at(state, end, true).terminal != kNoToken) break;
      }
      if ((end - begin) % kWorkBytes == 0) {
        ++_work;
        if (m.src + end >= m.mark) m.watch(m.src + end);
      }
    }
    if (end >= bound && bound < offset(m.end)) _work = std::max(_work, _limit + 1);
    ++_work;
  }
  if (!spent()) _unread.emplace(key, end);
  return end;
}

// The misspellings the text at `at` reads as in `state`, their walk counted
// as work and reading no further than the search's share allows.
const std::vector<Typo>& Repair::typos_at(std::uint32_t state, TextOffset at) {
  const std::uint64_t key = std::uint64_t{state} << 32 | at;
  if (const auto hit = _typos.find(key); hit != _typos.end()) return hit->second;
  const Token whole = mode_token(t.modeFault[t.stateMode[state]], at);
  const TextOffset plain = whole.terminal() != t.terminalCount ? offset(whole.end) : at;
  LexWork work{lex_room()};
  const ModeLexer mode(m, state);
  std::vector<Typo> found =
      typos(mode, {mode.follow, mode.follow + t.followWords}, at, plain, work);
  // A single literal opener may replace another opener the state expects.
  constexpr std::string_view openers = "([{";
  if (plain - at == 1 && !(m.rank(whole) & 1) && openers.find(m.src[at]) != openers.npos) {
    for (const char other : openers) {
      if (other == m.src[at]) continue;
      if (!work.spend()) break;
      const auto to = mode.step(1, other);
      if (!to) continue;
      const Ending e = mode.ending(to, plain, work);
      if (e.terminal != kNoToken && e.terminal != whole.terminal() && e.end == plain &&
          !(m.rank(e.terminal, e.literal) & 1) && t.inSet(t.followOf[state], e.terminal))
        found.push_back({plain, e.terminal, kEditCost, true});
    }
  }
  charge(work);
  if (!work.cut) return _typos.try_emplace(key, std::move(found)).first->second;
  _cutTypos = std::move(found);
  return _cutTypos;
}

// Whether refused reading `r` may go on by reading its token again.
bool Repair::rereads(const Candidate& r) {
  if (r.tok.terminal() == kEndOfInput) return false;
  const std::uint32_t state = _stacks.state(r.node);
  if (unclosed_at(state, r.tok.begin(m)).terminal != kNoToken) return true;
  return (r.phase != Phase::discardOnly && !typos_at(state, r.tok.begin(m)).empty()) || spent();
}

// The reading of `from` with its token's bytes up to `end` read as `term`,
// or skipped as the trivia they are. Whether it holds.
bool Repair::reread(std::uint32_t from, Edit edit, std::uint32_t cost) {
  const Edit::Kind kind = edit.kind;
  const TextOffset end = edit.end;
  const std::uint32_t e = child(from, edit);
  Candidate& r = _readings[e];
  r.cost += cost;
  r.lost += kind == Edit::substitute;
  r.unneeded += kind == Edit::substitute;
  r.freeInserts = 0;
  if (kind == Edit::skip) {
    r.at = m.src + end;
    r.held = false;
    r.resume = 2 * _stacks.state(r.node);
    r.phase = Phase::discardOnly;
  } else {
    r.tok = _links[e].written(m);
    r.held = true;
    r.resume = 2 * _stacks.state(r.node) + 1;
    r.pin = Pin::reread;
  }
  const Work work = read(e);
  const bool holds = work != Work::none && _readings[e].goal();
  admit(e, work);
  return holds;
}

// Whether an end at `at` would cut a scalar, or a word, in two.
bool Repair::cuts(TextOffset at) const {
  const std::string_view text(m.src, m.end - m.src);
  if (at >= text.size()) return false;
  if (detail::scalar_start(text, at) != at) return true;
  return at && m.word_at(m.src + at) && m.word_at(m.src + detail::scalar_start(text, at - 1));
}

// The next end the unclosed `reading` from `p` may stop at, or false.
bool Repair::more_ends(Ends& ends, TextOffset p, const Unclosed& reading) {
  if (ends.done) return false;
  const auto eol = [&](TextOffset i) { return m.src[i] == '\n' || m.src[i] == '\r'; };
  LexWork work{lex_room()};
  const auto until = [&](TextOffset x, auto stop) {
    for (; x < reading.end && work.spend(); ++x) {
      if (m.src + x >= m.mark) m.watch(m.src + x);
      if (stop(x)) break;
    }
    return x;
  };
  auto regime = ends.regime;
  TextOffset x = ends.scanned;
  if (regime == Ends::none) {
    x = until(p, eol);
    regime = x < reading.end ? Ends::lines
        : reading.end < offset(m.end) && eol(reading.end) ? Ends::line : Ends::last;
    if (regime == Ends::line) x = p + 1;
  } else if (regime == Ends::lines) {
    x = until(x, eol);
  }
  if (regime == Ends::line) x = until(x, [&](TextOffset i) { return !cuts(i); });
  charge(work);
  if (work.cut) return false;
  ends.regime = regime;
  ends.found.push_back(x);
  ends.scanned = x + 1;
  ends.done = x >= reading.end;
  return true;
}

// The token in hand as the unclosed token the lexer was refused there:
// read to where it closes, to each end it may stop at, or skipped where it
// is trivia. In a section, one that never closes ends where the token that
// closes the section begins.
void Repair::lexical(std::uint32_t at, std::uint32_t& choice) {
  const Candidate& b = _readings[at];
  const std::uint32_t state = _stacks.state(b.node);
  const TextOffset p = b.tok.begin(m);
  const Unclosed reading = unclosed_at(state, p);
  if (reading.terminal == kNoToken) return;
  const bool trivia = t.terminalTrivia[reading.terminal] != Trivia::none;
  if (!trivia && (b.phase == Phase::discardOnly || !t.inSet(t.followOf[state], reading.terminal)))
    return;
  const auto one = [&](TextOffset end) {
    return reread(at, {.choice = choice++, .kind = trivia ? Edit::skip : Edit::rep,
                       .term = reading.terminal, .end = end}, kRereadCost);
  };
  if (reading.closed) {
    one(reading.end);
    return;
  }
  const std::uint64_t key = std::uint64_t{reading.end} << 32 | p;
  for (std::size_t k = 0; !spent(); ++k) {
    Ends& ends = _ends[key];
    if (k == ends.found.size() && !more_ends(ends, p, reading)) return;
    const TextOffset end = ends.found[k];
    const bool lines = ends.regime == Ends::lines;
    if (one(end) && lines) return;
  }
}

// The word in hand read as the one keyword its state acts on, over the span
// its deletion takes: one edit at the cost of both.
void Repair::replacement(std::uint32_t from, std::uint32_t rank) {
  const std::uint32_t state = _stacks.state(_readings[from].node);
  const Token tok = _readings[from].tok;
  const std::uint16_t term = t.forced[state];
  const TextOffset end = deleted_end(state, tok);
  for (const Typo& typo : typos_at(state, tok.begin(m)))
    if (typo.end == end && typo.terminal == term) return;
  if (spent() || (m.src + end < m.end && m.word_at(m.src + end))) return;
  reread(from, {.choice = rank, .kind = Edit::rep, .term = term, .end = end}, kReplaceCost);
}

// The insertions of refused reading `from` whose run its state may require,
// or the others: whether any was passed over. A required one not due ranks
// behind every edit of its token.
bool Repair::insertions(std::uint32_t from, std::uint32_t rank, bool required) {
  const std::uint32_t state = _stacks.state(_readings[from].node);
  bool passed = false;
  each_insert(state, [&](std::uint16_t term, std::uint32_t k) {
    if (spent()) return false;
    if (t.inSet(t.mayCloseOf[state], term) != required) {
      passed = true;
      return true;
    }
    const std::uint32_t made = invent(from, term, rank + k);
    const Work work = read(made);
    if (required && !_readings[made].due) _links[made].choice += t.terminalCount;
    admit(made, work);
    return true;
  });
  return passed;
}

// The edits of a refused reading, in rank order: its token read as the
// unclosed one it is, or as a misspelling, which joins no literal the lexer
// read whole to what follows; as the keyword its state is forced to; the
// insertions it may need, the others behind one entry; then the deletion of
// its token.
void Repair::expand(std::uint32_t at) {
  const Candidate b = _readings[at];
  const std::uint32_t state = _stacks.state(b.node);
  const bool atEnd = b.tok.terminal() == kEndOfInput;
  const bool replaces = b.phase != Phase::discardOnly && !atEnd;
  std::uint32_t choice = 0;
  if (!atEnd) lexical(at, choice);
  const bool spelt =
      b.tok.terminal() < t.terminalCount && (m.rank(b.tok) & 1) == 0;
  if (replaces)
    for (const Typo& typo : typos_at(state, b.tok.begin(m))) {
      if (spent()) break;
      if (spelt && m.src + typo.end > b.tok.end) continue;
      ++_work;
      reread(at, {.choice = choice++, .kind = typo.substitutes ? Edit::substitute : Edit::rep,
                  .term = typo.terminal, .end = typo.end}, typo.cost);
    }
  // What the fault mode reads where the state's own read no token.
  const bool unread = !atEnd && b.tok.terminal() == t.terminalCount &&
                      mode_token(t.modeFault[t.stateMode[state]], b.tok.begin(m)).terminal() == t.terminalCount;
  const bool word = !atEnd && !unread;
  // A word in place of the one keyword its state acts on stands for it,
  // whatever it resembles: behind one entry, at the cost of both edits.
  if (replaces && word && t.forced[state] != kNoToken && m.word_at(m.src + b.tok.begin(m)))
    queue(b.cost + kReplaceCost, b, at, Work::replacement, choice);
  ++choice;
  // Bytes no terminal reads refuse every reading alike: insertions before
  // them are told apart only past them, so one is made there, never a run.
  const bool garbage =
      unread && unclosed_at(state, b.tok.begin(m)).terminal == kNoToken;
  if (b.phase == Phase::beforeEdit || (b.phase == Phase::inserting && !garbage)) {
    const std::uint32_t rank = choice;
    choice += 2u * t.terminalCount;
    // Only a required run costs nothing past its first: the others wait
    // until a reading that costly is asked for.
    if (insertions(at, rank, true))
      queue(b.cost + kEditCost, b, at, Work::insertions, rank + t.terminalCount);
  }
  if (!atEnd) {
    const TextOffset end = deleted_end(state, b.tok);
    const std::uint32_t e = child(at, {.choice = choice, .kind = Edit::del, .end = end});
    Candidate& r = _readings[e];
    indentation_dropped(r.indented, b.tok);
    if (m.src + end > r.at) r.at = m.src + end;
    r.held = false;
    r.resume = 2 * _stacks.state(r.node);
    r.cost += kEditCost;
    r.lost += 1;
    r.phase = Phase::discardOnly;
    r.freeInserts = 0;
    admit(e, read(e));
  }
}

// Whether `word` is a word that reads as a literal misspelt, in the mode the
// text of a fault is read in: one a literal spells does not. Asked once a
// spelling.
bool Repair::misspelt(Word word) {
  if (word.begin == word.end || !m.word_at(m.src + word.begin)) return false;
  if (_literals.empty()) {
    _literals.assign(t.followWords, 0);
    for (std::uint32_t term = 1; term < t.terminalCount; ++term)
      if ((t.terminalRank[term] & 1) == 0) _literals[term / 64] |= std::uint64_t{1} << (term % 64);
  }
  const ModeLexer mode(m, m.under(), true);
  const std::pair key{mode.mode, std::string_view(m.src + word.begin, m.src + word.end)};
  if (const auto known = _misspelt.find(key); known != _misspelt.end()) return known->second;
  bool misspells = false;
  if (const Token whole = mode_token(mode.mode, word.begin);
      offset(whole.end) != word.end || whole.terminal() >= t.terminalCount ||
      (m.rank(whole) & 1) != 0) {
    LexWork work{lex_room()};
    const std::vector<Typo> found = typos(mode, _literals, word.begin, word.end, work);
    charge(work);
    misspells = std::ranges::any_of(found, [&](const Typo& typo) { return typo.end == word.end; });
  }
  if (!spent()) _misspelt.emplace(key, misspells);
  return misspells;
}

// The machine read on up to `word` and stood before it, the word in hand.
// False where it does not stand there.
bool Repair::stand(Word word) {
  const bool stands = stood(word, m.tok, [&] {
    // A token it holds past the cut is none of the text cut there: the state
    // on top reads again.
    if (m.tok.end > m.end) {
      m.forget(m.at);
      m.resume = 2 * m.under();
    }
    return engine::run(m);
  });
  m.tok = unread(word);
  return stands;
}

// The machine set back before the first word that reads as a keyword
// misspelt, of the last it read since it set out and the one in hand. False
// with none, and where the text does not read up to that word alone.
bool Repair::back() {
  if (_marked != Marked::fresh) return false;
  std::uint64_t scanned = 0;
  unsigned tokens = 0;
  // The token in hand first: the states reduced on it as on the name it reads as.
  Word word{m.tok.begin(m), offset(m.tok.end)};
  bool found = m.tok.terminal() < t.terminalCount && misspelt(word);
  for (NodeCount i = m.tape.records(); i != _mark.records && tokens != kBackTokens && !spent();) {
    const CstNode& n = m.tape.at(--i);
    ++scanned;
    if (n.subtreeSize != 1 || n.isHidden() || n.isRecovered() || n.begin == n.end) continue;
    ++tokens;
    if (misspelt({n.begin, n.end})) {
      word = {n.begin, n.end};
      found = true;
    }
  }
  _work += scanned / kWorkBytes;
  if (!found || spent()) return false;
  _marked = Marked::back;
  _refused = m.tok;
  _refusedIn = m.indented;
  _refusedOn.clear();
  for (const Frame* f = m.frames; f != m.top; ++f) _refusedOn.push_back(f->state);
  _work += _refusedOn.size() / kWorkBytes;
  m.rewind(_mark);
  return stand(word);
}

// The machine set out again from where it last did, up to the token it was
// refused.
void Repair::forth() {
  m.rewind(_mark);
  [[maybe_unused]] const Exit exit = engine::run(m);
  assert(exit == Exit::refused && m.tok.head == _refused.head && m.tok.end == _refused.end);
  _marked = Marked::used;
}

// The machine's stack as the root of a search, queued as refused. The root
// of a machine set back reads on to the token refused; false where the text
// does not read from there as it did.
bool Repair::start() {
  // The search stands on the machine's stack, which only its plan changes.
  _stacks.clear({m.frames, static_cast<std::size_t>(m.top - m.frames)});
  _readings.clear();
  _links.clear();
  _heap.clear();
  _seen.clear();
  _typos.clear();
  _unclosed.clear();
  _completions.clear();
  _walks.clear();
  _walked.clear();
  _ends.clear();
  _goals.clear();
  _under.clear();
  _named.clear();
  _also.clear();
  _plan.clear();
  _seq = 0;
  _costBase = _cost;
  Candidate root;
  root.node = static_cast<std::uint32_t>(m.top - m.frames) - 1;
  root.at = m.at;
  root.tok = m.tok;
  root.indented = m.indented;
  _links.emplace_back();
  root.past = offset(m.at);
  // A fault a path left open goes on.
  root.fresh = !_fault.open;
  if (_marked != Marked::back) {
    root.resume = 2 * m.under() + 1;
    root.held = true;
    // A machine stopped at a split the text left undecided: its actions are
    // read on before any edit.
    const Work work = split_at(root).second != 0 ? Work::fork : Work::read;
    _readings.push_back(root);
    queue(work == Work::fork ? 0 : kRereadCost, root, 0, work);
    return true;
  }
  root.resume = 2 * m.under();
  _readings.push_back(root);
  // The text reads on as it did: to the same token, on the same stack.
  const Work stopped = read(0);
  const Candidate& stood = _readings[0];
  if (stopped == Work::none || !stood.held || stood.tok.head != _refused.head ||
      stood.tok.end != _refused.end || stood.indented != _refusedIn)
    return false;
  std::uint32_t node = stood.node;
  for (std::size_t i = _refusedOn.size(); i-- != 0; node = _stacks.parent(node))
    if (node == kNoStack || _stacks.state(node) != _refusedOn[i]) return false;
  _work += _refusedOn.size() / kWorkBytes;
  if (node != kNoStack) return false;
  queue(stopped == Work::fork ? 0 : kRereadCost, stood, 0, stopped);
  return true;
}

// The reading elected: a goal of the least cost, or with none before the
// work ran out, the one that went past the most of the author's tokens.
std::uint32_t Repair::run(bool& exhausted, bool& raced) {
  std::uint32_t furthest = kNoReading;
  std::uint64_t most = 0;
  Candidate best;
  exhausted = false;
  while (!_heap.empty()) {
    throwIfCancelled(m.stop);
    std::ranges::pop_heap(_heap, kEntryAfter);
    const Entry e = _heap.back();
    _heap.pop_back();
    if ((e.work == Work::read || e.work == Work::fork) && _readings[e.at].gone) continue;
    if (!_goals.empty() && e.f > _readings[_goals[0]].cost) break;
    // Advance a validated repair while comparing more expensive edits. Once it
    // reaches EOF, only candidates that can win or name an equal-cost edit matter.
    if (best.node != kNoStack && key_order(e.f, e, best.cost, best) > 0 &&
        cost_order(e.f, e, best.cost, best) > 0) {
      if (best.done) continue;
      // A token further on, the splits on the way decided as any step decides them.
      Candidate ahead = best;
      const Exit exit = step(ahead);
      if (exit == Exit::accepted) {
        best.done = true;
        continue;
      }
      if (exit == Exit::shifted) best.reading() = ahead.reading();
      else best.node = kNoStack;
    }
    if (spent()) {
      exhausted = true;
      break;
    }
    if (e.work == Work::fork) {
      forks(e.at);
      continue;
    }
    if (e.work == Work::insertions) {
      insertions(e.at, e.rank, false);
      continue;
    }
    if (e.work == Work::replacement) {
      replacement(e.at, e.rank);
      continue;
    }
    if (_readings[e.at].goal() && !_readings[e.at].done) refused_at_end(e.at);
    const Candidate& x = _readings[e.at];
    if (x.goal()) {
      if (best.node == kNoStack || (x.done != best.done ? x.done : key_order(x.cost, x, best.cost, best) < 0))
        best = x;
      _goals.push_back(e.at);
      continue;
    }
    if (const std::uint64_t went = std::uint64_t{x.tokens} + x.lost;
        furthest == kNoReading ||
        std::pair{went, x.at} > std::pair{most, _readings[furthest].at}) {
      furthest = e.at;
      most = went;
    }
    // Queued as if a re-reading went on from it: with none, it costs a whole
    // edit.
    if (x.freeInserts == 0 && e.f < x.cost + kEditCost && !rereads(x)) {
      queue(x.cost + kEditCost, x, e.at, Work::read);
      continue;
    }
    expand(e.at);
  }
  exhausted = exhausted || spent();
  return _goals.empty() ? furthest : elect(raced);
}

// The goals read on with no edit, the one furthest back first, until all
// that are left are of one set of goals or the race's steps pass: `reach[k]`
// is where goal `k`'s furthest reading was refused. True when a work limit
// stops readings that still disagree.
bool Repair::race(std::vector<TextOffset>& reach) {
  struct Runner { Candidate reading; std::set<std::uint32_t> goals; };
  std::vector<Runner> runners;
  std::vector<std::uint32_t> racing;
  const auto behind = [&](std::uint32_t a, std::uint32_t b) {
    return runners[a].reading.at > runners[b].reading.at;
  };
  const auto enqueue = [&](std::uint32_t i) {
    racing.push_back(i);
    std::ranges::push_heap(racing, behind);
  };
  std::ranges::fill(reach, 0);
  for (std::uint32_t g = 0; g < _goals.size(); ++g) {
    if (_readings[_goals[g]].done) {
      reach[g] = kReachAll;
      continue;
    }
    runners.push_back({_readings[_goals[g]], {g}});
    enqueue(static_cast<std::uint32_t>(runners.size() - 1));
  }
  const auto reach_all = [&](std::uint32_t i) {
    for (const std::uint32_t k : runners[i].goals) reach[k] = kReachAll;
  };
  // Whether what races is of one set of goals no other can reach.
  const auto level = [&] {
    const auto& first = runners[racing[0]].goals;
    const char* least = m.end;
    for (const std::uint32_t q : racing) {
      if (runners[q].goals != first) return false;
      least = std::min(least, runners[q].reading.at);
    }
    for (std::uint32_t k = 0; k < reach.size(); ++k)
      if (!first.contains(k) && reach[k] >= offset(least)) return false;
    return true;
  };
  std::unordered_map<Future, std::uint32_t, FutureHash> waiting;
  for (std::uint32_t steps = 0; !racing.empty(); ++steps) {
    throwIfCancelled(m.stop);
    if (const bool decided = level(); decided || steps >= kRaceSteps || spent()) {
      for (const std::uint32_t q : racing) reach_all(q);
      return !decided;
    }
    std::ranges::pop_heap(racing, behind);
    const std::uint32_t i = racing.back();
    racing.pop_back();
    if (const auto it = waiting.find(future(runners[i].reading)); it != waiting.end() && it->second == i)
      waiting.erase(it);
    switch (step(runners[i].reading)) {
      case Exit::refused:
      case Exit::split:
        for (const std::uint32_t k : runners[i].goals)
          reach[k] = std::max(reach[k], runners[i].reading.tok.begin(m));
        break;
      case Exit::accepted:
        reach_all(i);
        break;
      default:
        // Readings that meet past a token race as one.
        if (const std::uint32_t kept = waiting.try_emplace(future(runners[i].reading), i).first->second;
            kept != i) {
          auto& goals = runners[kept].goals;
          auto joining = std::move(runners[i].goals);
          if (goals.size() < joining.size()) goals.swap(joining);
          goals.merge(joining);
        } else {
          enqueue(i);
        }
    }
  }
  return false;
}

// The goal that reads furthest, then of the least key, then of the first
// rank.
std::uint32_t Repair::elect(bool& raced) {
  std::vector<TextOffset> reach(_goals.size(), kReachAll);
  if (reach.size() > 1) raced = race(reach);
  std::size_t won = 0;
  for (std::size_t g = 1; g < _goals.size(); ++g) {
    const Candidate& a = _readings[_goals[g]];
    const Candidate& b = _readings[_goals[won]];
    if (reach[g] != reach[won]) {
      if (reach[g] > reach[won]) won = g;
      continue;
    }
    const auto k = key_order(a.cost, a, b.cost, b);
    if (k != 0 ? k < 0 : before(part(_goals[g], _goals[won]))) won = g;
  }
  // What reads as far as the winner, as cheaply, names what its first written
  // terminal could have been.
  for (std::size_t g = 0; g < _goals.size(); ++g) {
    const Candidate& a = _readings[_goals[g]];
    const Candidate& b = _readings[_goals[won]];
    if (reach[g] == reach[won] && cost_order(a.cost, a, b.cost, b) == 0) name(_goals[g]);
  }
  return _goals[won];
}

// The path of `reading` as steps, one an edit.
void Repair::plan_path(std::uint32_t reading) {
  // Walk back from the goal. An edit closes the fault if the next edit
  // opens one; a split action carries only the cost to the next step.
  bool closes = _readings[reading].goal();
  auto cost = _readings[reading].cost;
  ElementId first = kNoElement;
  _plan.clear();
  for (; _links[reading].parent != kNoLink; reading = _links[reading].parent) {
    const Edit& edit = _links[reading];
    _plan.push_back({edit, closes, cost - edit.cost});
    cost = edit.cost;
    if (edit.kind == Edit::arm) continue;
    first = writes(edit) ? edit.term : kNoElement;
    closes = edit.opens;
  }
  // What its first written terminal could as cheaply have been, in the
  // grammar's order.
  if (first != kNoElement)
    std::erase(_also, first);
  else
    _also.clear();
  std::ranges::sort(_also, {}, [&](ElementId term) { return t.terminalOrder[term]; });
}

void Repair::open_span(TextOffset from, std::uint64_t least) {
  if (least < _spanLeast) _spanLeast = least;
  if (_spanOpen) return;
  _spanOpen = true;
  _spanBegin = _spanEnd = from;
}

void Repair::note_span() {
  if (!_spanOpen || _cost < _spanLeast) return;
  _spanOpen = false;
  _spanLeast = ~std::uint64_t{0};
  ParseDiagnostic n;
  n.kind = DiagnosticKind::Bounded;
  n.begin = _spanBegin;
  n.end = std::max(_spanEnd, _spanBegin);
  if (!_notes.empty() && _notes.back().end > n.end) n.end = _notes.back().end;
  _notes.push_back(std::move(n));
}

void Repair::note() {
  Fault& f = _fault;
  if (!f.open) return;
  ParseDiagnostic n;
  n.kind = DiagnosticKind::Deleted;
  n.element = f.resume;
  n.begin = f.begin;
  n.end = f.end;
  n.also = std::move(f.also);
  if (f.read != kNoElement || (f.first != kNoElement && f.del)) {
    n.kind = DiagnosticKind::Replaced;
    n.element = f.read != kNoElement ? f.read : f.first;
  } else if (f.first != kNoElement) {
    n.kind = DiagnosticKind::Inserted;
    n.element = f.first;
    n.begin = n.end = f.point;
  } else n.mixedIndent = f.mixedIndent;
  if (_spanOpen && n.begin < _spanBegin) _spanBegin = n.begin;
  _notes.push_back(std::move(n));
  f = Fault{};
}

Exit Repair::shift(bool settles, bool repairs) {
  Repairer reader(m, settles, repairs);
  for (;;) {
    const Exit exit = t.repair(reader);
    if (exit != Exit::split) return exit;
    m.resume = split_arm();
  }
}

// `token` written in place of the one in hand, with the reductions it makes.
// False where the states refuse it: the token in hand is back.
bool Repair::write_token(Token token, bool settles) {
  const Token held = m.tok;
  m.tok = token;
  m.resume = 2 * m.under() + 1;
  if (shift(settles, true) == Exit::shifted) return true;
  m.tok = held;
  m.resume = 2 * m.under() + 1;
  return false;
}

// Whether the path's next step is the action taken at the split the states
// stand at.
bool Repair::arm_here() const {
  return !_plan.empty() && _plan.back().edit.kind == Edit::arm && _plan.back().edit.matches(m);
}

// The action a split met while a step is written goes on from: the path's,
// where its next step is taken there; else the text read ahead's, else the
// first.
std::uint32_t Repair::split_arm() {
  if (arm_here()) {
    const Edit& g = _plan.back().edit;
    _cost += _plan.back().cost;
    _plan.pop_back();
    return g.resume;
  }
  const std::uint32_t next = arm(m);
  return next != kNoStack ? next : m.arms;
}

// The token insertion `edit` writes, before the token in hand, which the
// states then read again.
bool Repair::write_insert(const Edit& edit) {
  // The trivia read past the point is read again with the token in hand.
  // What stands before the point is written first: text a repair skipped,
  // and before the first token the comments in front of the text.
  std::vector<Machine::Hidden> later;
  if (m.at != m.src) {
    const auto from = std::ranges::find_if(
        m.pending, [&](const Machine::Hidden& x) { return x.begin >= offset(m.at); });
    later.assign(from, m.pending.end());
    m.pending.erase(from, m.pending.end());
  }
  if (!write_token(edit.written(m), !m.pending.empty())) {
    m.pending.insert(m.pending.end(), later.begin(), later.end());
    return false;
  }
  m.pending.clear();
  return true;
}

// Advance past deleted text or repaired trivia, keeping its hidden record.
void Repair::skip(const Edit& edit) {
  const TextOffset begin = std::max(m.tok.begin(m), offset(m.at));
  TextOffset end = edit.end;
  if (edit.kind == Edit::del) {
    indentation_dropped(m.indented, m.tok);
    if (!end) end = deleted_end(m.under(), m.tok);
    m.hide(begin, m.src + std::max(end, begin), kSkippedElement);
  } else if (t.terminalTrivia[edit.term] == Trivia::hidden && end > begin) {
    m.hide(begin, m.src + end, t.terminalEntry[edit.term]);
  }
  m.at = std::max(m.at, m.src + end);
  m.resume = 2 * m.under();
}

bool Repair::write(const Step& st) {
  _marked = Marked::stale;
  // The action the path took at a split, where the text read ahead could not
  // tell.
  if (st.edit.kind == Edit::arm) {
    m.resume = st.edit.resume;
    _cost += st.cost;
    return true;
  }
  Fault& f = _fault;
  if (st.edit.opens) note();
  if (!f.open) {
    f.open = true;
    f.point = offset(m.at);
    f.begin = f.end = m.tok.begin(m);
  }
  // The first edit that takes bytes says where the fault begins.
  const bool taken = f.read != kNoElement || f.del;
  // The terminal the note names lists what the path's first written
  // terminal could as cheaply have been, when it is that one.
  if (st.edit.kind == Edit::ins) {
    if (!write_insert(st.edit)) return false;
    if (f.first == kNoElement) {
      f.first = st.edit.term;
      if (f.read == kNoElement) f.also = std::exchange(_also, {});
    }
  } else if (st.edit.kind != Edit::del) {
    const TextOffset begin = m.tok.begin(m);
    if (st.edit.kind == Edit::skip) skip(st.edit);
    else if (!write_token(st.edit.written(m))) return false;
    if (st.edit.kind == Edit::skip || f.first != kNoElement || f.read != kNoElement) f.also.clear();
    else f.also = std::exchange(_also, {});
    f.read = st.edit.term;
    if (!taken) f.begin = begin;
    f.end = st.edit.end;
  } else {
    if (!taken) {
      f.begin = m.tok.begin(m);
      f.mixedIndent = (m.tok.head & kMixedIndent) != 0;
    }
    f.del = true;
    skip(st.edit);
    f.end = offset(m.at);
    const std::uint16_t mode = t.stateMode[m.under()];
    f.resume = t.modes[mode].read(m, m.at, mode).terminal();
  }
  _cost += st.cost;
  if (st.closes) note();
  return true;
}

// At the end of the text, the insertions the stack still needs, in a row:
// each the first its state offers that begins the stack's cheapest
// completion, until the text is read or none does. Whether it made any: its
// path is then the plan.
bool Repair::closers() {
  start();
  // Its own share: the parse's may be spent when it is asked for.
  _limit = _work + kWorkPerSearch;
  // Each time at one place, the stack must come closer to whole.
  const std::uint32_t whole = completion(_readings[0].node);
  if (m.at == _closedAt && whole >= _closedLeast) return false;
  _closedAt = m.at;
  _closedLeast = whole;
  std::uint32_t at = 0;
  while (!_readings[at].done && !spent()) {
    const std::uint32_t state = _stacks.state(_readings[at].node);
    std::uint32_t took = kNoReading;
    each_insert(state, [&](std::uint16_t term, std::uint32_t) {
      if (!t.inSet(t.mayCloseOf[state], term)) return true;
      const std::size_t mark = _readings.size();
      std::uint32_t e = invent(at, term, 0);
      // A split the text left undecided goes on from its first action.
      Work work = read(e);
      while (work == Work::fork) {
        e = child(e, {.kind = Edit::arm, .resume = split_at(_readings[e]).first});
        work = read(e);
      }
      if (work != Work::none && _readings[e].due) {
        took = e;
        return false;
      }
      discard(mark);
      return true;
    });
    if (took == kNoReading) break;
    at = took;
  }
  if (at == 0) return false;
  plan_path(at);
  return true;
}

// The first edit that holds, with no search: the token in hand dropped, or
// at the end of the text the insertions the stack still needs.
bool Repair::degrade() {
  // No token read since the last of them: the same fault goes on.
  if (m.at != _degradedAt) note();
  open_span(m.tok.begin(m), _cost + kRereadCost);
  if (m.tok.terminal() != kEndOfInput) {
    const Edit deletion{.kind = Edit::del};
    write({deletion, false, kEditCost});
    _degradedAt = m.at;
    _spanEnd = offset(m.at);
    return true;
  }
  if (closers() && apply()) return true;
  note();
  return false;
}

// The next step of the plan written: false where the states refuse it, and
// the plan is dropped.
bool Repair::apply() {
  const Step step = _plan.back();
  _plan.pop_back();
  if (!write(step)) {
    _plan.clear();
    return false;
  }
  if (_plan.empty() && _fault.open) _openAt = m.at;
  if (_degraded) _degradedAt = m.at;
  if (_spanOpen) {
    _spanEnd = offset(m.at);
    if (_plan.empty() && !_fault.open && !_degraded) note_span();
  }
  return true;
}

// The first step of the elected path written, the rest planned; `bound`
// says a work bound decided the election.
bool Repair::take(std::uint32_t elected, bool bound) {
  const bool back = _marked == Marked::back;
  if (back) _marked = Marked::used;
  const TextOffset from = (back ? _refused : m.tok).begin(m);
  if (bound) open_span(from, _costBase + kRereadCost);
  _degraded = _work > _budget;
  if (elected != kNoReading)
    plan_path(elected);
  else
    _plan.clear();
  // A machine set back reads on: to the word the path edits first, or to
  // the token it was refused.
  if (back) {
    m.forget(m.at);
    m.resume = 2 * m.under();
    return true;
  }
  if (!_plan.empty()) {
    if (apply()) return true;
    _degraded = true;
  }
  open_span(from, _costBase + kRereadCost);
  return degrade();
}

bool Repair::pause() {
  throwIfCancelled(m.stop);
  // What was read ahead of the token refused says nothing past an edit.
  m.plan.clear();
  m.planned = 0;
  // The plan's next step is taken where it was made; elsewhere the plan is
  // dropped.
  if (!_plan.empty()) {
    const Edit& edit = _plan.back().edit;
    if (edit.matches(m) && _cost == _costBase + edit.cost && apply()) return true;
    _plan.clear();
  }
  if (_degraded) return degrade();
  if (_openAt != m.at) {
    note();
    note_span();
  }
  _openAt = nullptr;
  _limit = _work < _budget ? _work + std::min(kWorkPerSearch, _budget - _work) : _budget;
  // A keyword misspelt may read as a name, and be refused a token later: the
  // search then sets out from before that word.
  if (!back() || !start()) {
    if (_marked == Marked::back) forth();
    start();
  }
  // Nor does what a machine set back read ahead on its way.
  m.plan.clear();
  m.planned = 0;
  bool exhausted = false;
  bool raced = false;
  const std::uint32_t elected = run(exhausted, raced);
  return take(elected, exhausted || raced);
}

// The states read on. Text skipped is written before the next token, which
// only the states of a language that hides trivia see to: for another, that
// token is read here.
Exit Repair::onward() {
  if (!t.hides && !m.pending.empty()) {
    const Exit exit = shift(false, false);
    if (exit != Exit::shifted) return exit;
  }
  const bool word = !_plan.empty() && _plan.back().edit.word;
  if (_backs && _marked == Marked::stale && (_plan.empty() || word) && !_degraded) {
    m.checkpoint(_mark);
    _marked = Marked::fresh;
  }
  // The next step edits a word the states read: the machine is stood before
  // it, or with no way back from there, reads on without the plan.
  if (word) {
    const Token tok = _plan.back().edit.tok;
    if (_marked != Marked::stale && stand({tok.begin(m), offset(tok.end)})) return Exit::refused;
    _plan.clear();
    if (_marked != Marked::stale) m.rewind(_mark);
  }
  // The states stop at a split where the path's next step takes an action,
  // which the next pause writes, unless they read only up to a caret.
  const Exit exit = engine::run(m, nullptr, [&] { return m.ended == kEndOfInput && arm_here(); });
  return exit == Exit::split ? Exit::refused : exit;
}

void Repair::finish(CstBuilder& builder) {
  note();
  note_span();
  for (ParseDiagnostic& n : _notes)
    builder.noteRepair(n.kind, n.element, n.begin, n.end, std::move(n.also), n.mixedIndent);
}

}  // namespace

bool repair(Machine& machine, CstBuilder& builder, const char* whole) {
  Repair search(machine, whole == nullptr);
  const char* const caret = machine.end;
  const std::uint16_t ended = machine.ended;
  Exit exit = Exit::refused;
  while (exit == Exit::refused &&
         (whole == nullptr || machine.src + machine.tok.begin(machine) < caret)) {
    // A search reads the text past a caret; the states read on up to it.
    if (whole != nullptr) {
      machine.read_to(whole, kEndOfInput);
    }
    const bool edited = search.pause();
    if (whole != nullptr) {
      machine.read_to(caret, ended);
      machine.at = std::min(machine.at, caret);
      if (machine.src + machine.tok.begin(machine) >= caret)
        machine.tok = token(static_cast<TextOffset>(caret - machine.src), caret, ended);
    }
    if (!edited) break;
    exit = search.onward();
  }
  search.finish(builder);
  return exit == Exit::accepted;
}

}  // namespace cursiv::engine
