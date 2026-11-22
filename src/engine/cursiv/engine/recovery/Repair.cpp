#include "cursiv/engine/recovery/Repair.hpp"

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <set>
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
// The work a parse may spend on its repairs: a floor and a share a byte, up
// to what bounds its latency; and the most one search takes of it.
constexpr std::uint64_t kWorkFloor = std::uint64_t{1} << 16;
constexpr std::uint64_t kWorkPerByte = 64;
constexpr std::uint64_t kWorkPerParse = std::uint64_t{1} << 21;
constexpr std::uint64_t kWorkPerSearch = std::uint64_t{1} << 19;
// The tokens a race between goals of one cost may read.
constexpr std::uint32_t kRaceSteps = 1024;

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
  [[nodiscard]] Token replacement(const Machine& m) const {
    Token result = token(tok.begin(m), m.src + end, term);
    if (kind == substitute) {
      result.head |= kReplaced | kTold;
      if (m.t.terminalRank[term] & 1) result.head |= kLiteralBranch;
    }
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
  // The required insertions it may still make at no cost.
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
  bool operator==(const Future&) const = default;
};
struct FutureHash {
  std::size_t operator()(const Future& f) const {
    return static_cast<std::size_t>((f.stack * 0x9E3779B97F4A7C15ull) ^ std::rotl(f.token, 29) ^
                                    std::rotl(f.flags, 47) ^ std::rotl(f.indented, 13));
  }
};

// Where two paths part: the link they share, and under it the link of each,
// none for a path that ends there.
struct Parting {
  std::uint32_t shared = kNoLink;
  std::uint32_t a = kNoLink;
  std::uint32_t b = kNoLink;
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
  explicit Repair(Machine& machine)
      : m(machine),
        t(machine.t),
        _budget(std::min(static_cast<std::uint64_t>(m.end - m.src) * kWorkPerByte + kWorkFloor,
                         kWorkPerParse)) {}

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
            std::uint64_t{r.indented.level} << 32 | r.indented.depth};
  }

  // The search.
  void start();
  std::uint32_t run(bool& exhausted, bool& raced);
  void expand(std::uint32_t at);
  bool insertions(std::uint32_t from, std::uint32_t rank, bool required);
  bool reread(std::uint32_t from, Edit::Kind kind, std::uint16_t term, TextOffset end,
              std::uint32_t cost, std::uint32_t choice);
  void lexical(std::uint32_t at, std::uint32_t& choice);
  void replacement(std::uint32_t from, std::uint32_t rank);
  bool more_ends(Ends& ends, TextOffset p, const Unclosed& reading);
  [[nodiscard]] bool cuts(TextOffset at) const;
  const Unclosed& unclosed_at(std::uint32_t state, TextOffset at, bool fault = false);
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
  Exit step(Candidate& r);
  void admit(std::uint32_t at, Work work);
  bool merge(std::uint32_t at, std::uint32_t& kept);
  void queue(std::uint32_t f, const Candidate& r, std::uint32_t at, Work work,
             std::uint32_t rank = 0);
  [[nodiscard]] Parting part(std::uint32_t a, std::uint32_t b) const;
  [[nodiscard]] bool before(const Parting& p) const;
  void tie(std::uint32_t kept, std::uint32_t dropped, const Parting& p, bool handed);
  void own(std::uint32_t from, std::uint32_t shared, std::vector<ElementId>& out) const;
  void add_name(std::uint32_t path, ElementId term);
  void name(std::uint32_t path);
  std::uint32_t elect(bool& raced);
  bool race(const std::vector<std::uint32_t>& of, std::vector<TextOffset>& reach);
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
  std::uint32_t split_arm();
  Exit shift(bool settles, bool repairs);
  bool write_token(Token token, bool settles = false);
  bool write_insert(std::uint16_t term);
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

  // One search: the stacks, the readings, the paths and the queue.
  // Where the end-of-text insertions were last made, how many, and how many
  // the stack found there may need.
  const char* _closedAt = nullptr;
  std::uint64_t _closed = 0;
  std::uint64_t _closeMost = 0;
  Cactus _stacks;
  // The stack and the actions a split met by a reading is decided with.
  std::vector<Frame> _frames;
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
  // Per mode and place, the unclosed token the lexer was refused there and
  // the ends found for it; per mode and place, the token read; per
  // mode and place its own reads no token at, where a deletion ends.
  std::unordered_map<std::uint64_t, Unclosed> _unclosed;
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

// Where paths `a` and `b` part, read from the root down.
Parting Repair::part(std::uint32_t a, std::uint32_t b) const {
  std::uint32_t x = a, y = b;
  while (x != y) {
    x = x == kNoLink ? b : _links[x].parent;
    y = y == kNoLink ? a : _links[y].parent;
  }
  Parting p{x};
  for (; a != x; a = _links[a].parent) p.a = a;
  for (; b != x; b = _links[b].parent) p.b = b;
  return p;
}

// Whether the first path of `p` ranks before the second: the lower choice
// where they part, and a path that ends there first.
bool Repair::before(const Parting& p) const {
  if (p.a == kNoLink || p.b == kNoLink) return p.a == kNoLink && p.b != kNoLink;
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
  discard(at);
}

// The reading of `from` with `term` put in hand before its token.
std::uint32_t Repair::invent(std::uint32_t from, std::uint16_t term, std::uint32_t choice) {
  const std::uint32_t e = child(from, {.choice = choice, .kind = Edit::ins, .term = term});
  Candidate& r = _readings[e];
  r.tok = token(offset(r.at), r.at, term);
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

// One token of `r`: shifted, refused in the state on top, the text's end, or
// a split the text read ahead left undecided, which `r` then stands at.
Exit Repair::step(Candidate& r) {
  const std::uint32_t from = r.node;
  const auto before = m.explored;
  Explorer x(m, _stacks, r);
  Exit exit = x.step();
  std::size_t planned = 0;
  while (exit == Exit::split) {
    // The text ahead decides, read from a copy of the reading's stack; its
    // plan serves the splits of the same token it met.
    if (planned == _arms.size() || _arms[planned] < x.arms ||
        _arms[planned] >= x.arms + x.armCount) {
      _frames.clear();
      for (std::uint32_t n = x.node; n != kNoStack; n = _stacks.parent(n))
        _frames.push_back({_stacks.state(n), 0});
      std::ranges::reverse(_frames);
      _arms.clear();
      planned = 0;
      resolve(m, _frames, x.at, x.tok, x.indented, x.arms, x.armCount, x.rereads, _arms);
      _work += _frames.size() / kWorkBytes;
      if (_arms.empty()) break;
    }
    x.resume = _arms[planned++];
    exit = x.step();
  }
  _arms.clear();
  _work += m.explored - before;
  // A reread that dropped the substitute cannot validate its shift.
  if (r.pin == Pin::reread && (r.tok.head & kReplaced) &&
      (exit == Exit::accepted || !((exit == Exit::shifted ? x.taken.head : x.tok.head) & kReplaced)))
    return Exit::refused;
  static_cast<Reading&>(r) = x;
  r.held = exit == Exit::refused || exit == Exit::split;
  if (r.held) return exit;
  if (exit == Exit::accepted) {
    r.done = true;
  } else if (r.pin == Pin::invented) {
    // An invented token is no token of the author's: the fault goes on. A
    // required insertion that goes on a run of them costs nothing; the
    // first of a run frees as many as the stack it was made on may need.
    r.pin = Pin::none;
    r.due = t.inSet(t.closeOf[_stacks.state(_stacks.parent(r.node))], x.taken.terminal());
    r.unneeded += r.due ? 0u : 1u;
    const bool goesOn = r.due && r.freeInserts != 0;
    r.cost += goesOn ? 0 : kEditCost;
    r.edits -= goesOn ? 1u : 0u;
    const std::uint64_t depth = _stacks.depth(from);
    const std::uint64_t most = t.closeMost != 0 && depth > ~std::uint64_t{0} / t.closeMost
                                   ? ~std::uint64_t{0}
                                   : depth * t.closeMost;
    r.freeInserts = !r.due ? 0 : goesOn ? r.freeInserts - 1 : most > 0 ? most - 1 : 0;
  } else {
    // The author's bytes read again are a token of theirs, but none past the
    // edit that read them; a reading validates past an edit only, and
    // indentation, made of blanks, validates nothing.
    const std::uint16_t taken = x.taken.terminal();
    if (r.pin == Pin::none && r.edits != 0 && r.shifts < kValidateShifts &&
        taken != t.indentTerminal && taken != t.dedentTerminal)
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
  for (;;) {
    Candidate& r = _readings[at];
    if (r.done || (r.pin == Pin::none && r.goal())) return Work::read;
    if (spent()) return Work::none;
    const Exit exit = step(r);
    if (exit == Exit::refused) return r.pin == Pin::none ? Work::read : Work::none;
    if (exit == Exit::split) return Work::fork;
  }
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

// The unclosed token the mode of `state` was refused at `at`, read once a
// search, its walk counted as work.
const Unclosed& Repair::unclosed_at(std::uint32_t state, TextOffset at, bool fault) {
  static constexpr Unclosed kNone{};
  if (m.src + at >= m.end || t.lexOpeners[byte_at(m.src + at)] == 0 || spent()) return kNone;
  const ModeLexer mode(m, state, fault);
  const auto key = std::uint64_t{fault ? mode.mode : state} << 33 | std::uint64_t{fault} << 32 | at;
  if (const auto hit = _unclosed.find(key); hit != _unclosed.end()) return hit->second;
  LexWork work{lex_room()};
  Unclosed found = unclosed(mode, at, work);
  charge(work);
  if (work.cut) return kNone;
  if (!fault && found.terminal != kNoToken && !found.closed && t.modeClose != nullptr) {
    const auto close = t.modeClose[mode.mode];
    for (TextOffset x = at + 1; close != kNoToken && x < found.end; ++x) {
      if ((x - at - 1) % kWorkBytes == 0) {
        if (++_work > _limit) return kNone;
        if (m.src + x >= m.mark) m.watch(m.src + x);
      }
      const auto first = t.transition(1, t.byteClass[byte_at(m.src + x)]);
      if (first == 0 || !t.inSet(t.lexStates[first].reach, close)) continue;
      if (mode_token(mode.mode, x).terminal() == close) {
        found.end = x;
        break;
      }
      if (spent()) return kNone;
    }
  }
  return _unclosed.emplace(key, found).first->second;
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
  std::vector<Typo> found = typos(m, state, at, plain, work);
  // A single literal opener may replace another opener the state expects.
  constexpr std::string_view openers = "([{";
  if (plain - at == 1 && !(m.rank(whole) & 1) && openers.find(m.src[at]) != openers.npos) {
    const ModeLexer mode(m, state);
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
bool Repair::reread(std::uint32_t from, Edit::Kind kind, std::uint16_t term, TextOffset end,
                    std::uint32_t cost, std::uint32_t choice) {
  const std::uint32_t e = child(from, {.choice = choice, .kind = kind, .term = term, .end = end});
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
    r.tok = _links[e].replacement(m);
    r.held = true;
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
    return reread(at, trivia ? Edit::skip : Edit::rep, reading.terminal, end, kRereadCost, choice++);
  };
  if (reading.closed) {
    one(reading.end);
    return;
  }
  const std::uint64_t key = std::uint64_t{t.stateMode[state]} << 32 | p;
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
  reread(from, Edit::rep, term, end, kReplaceCost, rank);
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
      reread(at, typo.substitutes ? Edit::substitute : Edit::rep, typo.terminal,
             typo.end, typo.cost, choice++);
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

// The machine's stack as the root of a search, queued as refused.
void Repair::start() {
  // The search stands on the machine's stack, which only its plan changes.
  _stacks.clear({m.frames, static_cast<std::size_t>(m.top - m.frames)});
  _readings.clear();
  _links.clear();
  _heap.clear();
  _seen.clear();
  _typos.clear();
  _unclosed.clear();
  _ends.clear();
  _goals.clear();
  _under.clear();
  _named.clear();
  _also.clear();
  _plan.clear();
  _seq = 0;
  _limit = _work < _budget ? _work + std::min(kWorkPerSearch, _budget - _work) : _budget;
  _costBase = _cost;
  Candidate root;
  root.node = static_cast<std::uint32_t>(m.top - m.frames) - 1;
  root.resume = 2 * m.under() + 1;
  root.at = m.at;
  root.tok = m.tok;
  root.indented = m.indented;
  root.held = true;
  _links.emplace_back();
  // A fault a path left open goes on.
  root.fresh = !_fault.open;
  // A machine stopped at a split the text left undecided: its actions are
  // read on before any edit.
  const Work work = split_at(root).second != 0 ? Work::fork : Work::read;
  _readings.push_back(root);
  queue(work == Work::fork ? 0 : kRereadCost, root, 0, work);
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
      Explorer proof(m, _stacks, best);
      const auto before = m.explored;
      const auto exit = proof.step();
      _work += m.explored - before;
      if (exit == Exit::accepted) {
        best.done = true;
        continue;
      }
      if (exit == Exit::shifted) static_cast<Reading&>(best) = proof;
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
// that are left are of one candidate or the race's steps pass: `reach[k]` is
// where candidate `k`'s furthest reading was refused. True when a work
// limit stops readings that still disagree.
bool Repair::race(const std::vector<std::uint32_t>& of, std::vector<TextOffset>& reach) {
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
      reach[of[g]] = kReachAll;
      continue;
    }
    runners.push_back({_readings[_goals[g]], {of[g]}});
    enqueue(static_cast<std::uint32_t>(runners.size() - 1));
  }
  const auto reach_all = [&](std::uint32_t i) {
    for (const std::uint32_t k : runners[i].goals) reach[k] = kReachAll;
  };
  // Whether what races is of one set of candidates no other can reach.
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
// rank. Goals that make the same edits race as one candidate.
std::uint32_t Repair::elect(bool& raced) {
  if (_goals.size() == 1) {
    name(_goals[0]);
    std::ranges::sort(_also, {}, [&](ElementId term) { return t.terminalOrder[term]; });
    return _goals[0];
  }
  const auto same = [&](std::uint32_t a, std::uint32_t b) {
    for (; a != b; a = _links[a].parent, b = _links[b].parent) {
      const Edit& x = _links[a];
      const Edit& y = _links[b];
      if (x.parent == kNoLink || y.parent == kNoLink || x.kind != y.kind || x.term != y.term ||
          x.end != y.end || x.at != y.at || x.tok.head != y.tok.head || x.resume != y.resume)
        return false;
    }
    return true;
  };
  // Paths that make the same edits fold the same: a goal is compared with
  // the candidates of its fold alone.
  const auto fold = [&](std::uint32_t l) {
    std::uint64_t h = 0;
    for (; _links[l].parent != kNoLink; l = _links[l].parent, ++_work) {
      const Edit& g = _links[l];
      h = (h ^ std::uint64_t{g.kind} << 56 ^ std::uint64_t{g.term} << 40 ^
           std::uint64_t{g.end} << 20 ^ g.tok.head ^ offset(g.at) ^ std::uint64_t{g.resume} << 24) *
          0x100000001b3ull;
    }
    return h ^ l;
  };
  std::vector<std::uint32_t> of(_goals.size());
  std::unordered_multimap<std::uint64_t, std::size_t> folded;
  for (std::size_t g = 0; g < _goals.size(); ++g) {
    const auto hash = fold(_goals[g]);
    const auto [first, last] = folded.equal_range(hash);
    const auto known = std::find_if(first, last, [&](const auto& entry) {
      return same(_goals[entry.second], _goals[g]);
    });
    if (known == last) {
      of[g] = static_cast<std::uint32_t>(folded.size());
      folded.emplace(hash, g);
    } else of[g] = of[known->second];
  }
  std::vector<TextOffset> reach(folded.size(), kReachAll);
  if (reach.size() > 1) raced = race(of, reach);
  std::size_t won = 0;
  for (std::size_t g = 1; g < _goals.size(); ++g) {
    const Candidate& a = _readings[_goals[g]];
    const Candidate& b = _readings[_goals[won]];
    if (reach[of[g]] != reach[of[won]]) {
      if (reach[of[g]] > reach[of[won]]) won = g;
      continue;
    }
    const auto k = key_order(a.cost, a, b.cost, b);
    if (k != 0 ? k < 0 : before(part(_goals[g], _goals[won]))) won = g;
  }
  // What reads as far as the winner, as cheaply, names what its first written
  // terminal could have been, in the grammar's order.
  for (std::size_t g = 0; g < _goals.size(); ++g) {
    const Candidate& a = _readings[_goals[g]];
    const Candidate& b = _readings[_goals[won]];
    if (reach[of[g]] == reach[of[won]] && cost_order(a.cost, a, b.cost, b) == 0) name(_goals[g]);
  }
  std::ranges::sort(_also, {}, [&](ElementId term) { return t.terminalOrder[term]; });
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
  if (first != kNoElement)
    std::erase(_also, first);
  else
    _also.clear();
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

// The action a split met while a step is written goes on from: the path's,
// where its next step is taken there; else the text read ahead's, else the
// first.
std::uint32_t Repair::split_arm() {
  if (!_plan.empty()) {
    const Edit& g = _plan.back().edit;
    if (g.kind == Edit::arm && g.matches(m)) {
      _cost += _plan.back().cost;
      _plan.pop_back();
      return g.resume;
    }
  }
  const std::uint32_t next = arm(m);
  return next != kNoStack ? next : m.arms;
}

// `term` written before the token in hand, which the states then read
// again.
bool Repair::write_insert(std::uint16_t term) {
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
  if (!write_token(token(offset(m.at), m.at, term), !m.pending.empty())) {
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
    if (!write_insert(st.edit.term)) return false;
    if (f.first == kNoElement) {
      f.first = st.edit.term;
      if (f.read == kNoElement) f.also = std::exchange(_also, {});
    }
  } else if (st.edit.kind != Edit::del) {
    const TextOffset begin = m.tok.begin(m);
    if (st.edit.kind == Edit::skip) skip(st.edit);
    else if (!write_token(st.edit.replacement(m))) return false;
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
    f.resume = t.modes[t.stateMode[m.under()]].read(m, m.at).terminal();
  }
  _cost += st.cost;
  if (st.closes) note();
  return true;
}

// At the end of the text, the insertions the stack still needs, in a row:
// each the first its state offers that its shift needs, until the text is
// read, nothing is needed or the bound is reached. Whether it made any: its
// path is then the plan.
bool Repair::closers() {
  start();
  // Its own share: the parse's may be spent when it is asked for.
  _limit = _work + kWorkPerSearch;
  // What was inserted here since the last token read counts: a stack that
  // an insertion leaves as it was is not closed by more of them.
  if (m.at != _closedAt) {
    _closedAt = m.at;
    _closed = 0;
    _closeMost = std::min(static_cast<std::uint64_t>(m.top - m.frames) * (t.closeMost + 1),
                          kWorkPerSearch);
  }
  std::uint32_t at = 0;
  for (; !_readings[at].done && _closed < _closeMost && !spent(); ++_closed) {
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
  const TextOffset from = m.tok.begin(m);
  if (bound) open_span(from, _costBase + kRereadCost);
  _degraded = _work > _budget;
  if (elected != kNoReading)
    plan_path(elected);
  else
    _plan.clear();
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
  start();
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
  return engine::run(m);
}

void Repair::finish(CstBuilder& builder) {
  note();
  note_span();
  for (ParseDiagnostic& n : _notes)
    builder.noteRepair(n.kind, n.element, n.begin, n.end, std::move(n.also), n.mixedIndent);
}

}  // namespace

bool repair(Machine& machine, CstBuilder& builder, const char* whole) {
  Repair search(machine);
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
