#include "cursiv/engine/lexer/LexAutomaton.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/grammar/StatePartition.hpp"
#include "cursiv/engine/grammar/StrongComponents.hpp"
#include "cursiv/engine/lexer/CharClass.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/Utf8.hpp"

namespace cursiv::engine::gen {


namespace {

constexpr auto kUnboundedCount = static_cast<std::uint64_t>(detail::kUnbounded);

// The out of a lead's fragment where only the matcher reads on.
constexpr std::uint32_t kStopped = ~std::uint32_t{0};

// The copies of a counted repetition an automaton unrolls.
constexpr std::uint64_t kUnrolled = 64;

// One character, as `any` reads it.
constexpr ImageNode kAnyChar = [] {
  ImageNode any;
  any.kind = ImageKind::cpset;
  any.flags = kCpNot;
  return any;
}();

// More states than any automaton the lexer can hold is made of: a terminal
// that inlines itself many times over stops here.
constexpr std::size_t kStateLimit = std::size_t{1} << 20;

bool ascii_letter(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// A gap reads regular text. A literal may test regular text at its end.
bool gap_pattern(const GrammarImage& image, ImageId id) {
  std::vector<ImageId> open{id};
  while (!open.empty()) {
    const ImageNode n = image.nodes[open.back()];
    open.pop_back();
    switch (n.kind) {
      case ImageKind::cpset:
      case ImageKind::byte:
        break;
      case ImageKind::lit:
        if ((n.flags & ~(kLitFolded | kLitKeyword | kLitGuardExpr)) != 0) return false;
        if ((n.flags & kLitGuardExpr) != 0) {
          const auto guard = image.children[n.first];
          const auto& g = image.nodes[guard];
          open.push_back(g.kind == ImageKind::notOf || g.kind == ImageKind::andOf
                             ? image.children[g.first] : guard);
        }
        break;
      case ImageKind::doc:
        open.push_back(image.children[n.first]);
        break;
      case ImageKind::seq:
      case ImageKind::choice:
      case ImageKind::repeat:
        for (std::uint32_t i = 0; i < n.count; ++i) open.push_back(image.children[n.first + i]);
        break;
      default:
        return false;
    }
  }
  return true;
}

// A lead byte read as one that starts no scalar holds only where the bytes
// after it do not complete its sequence. What is still awaited of them: one
// to three continuation bytes, or first one of the narrower range that
// follows E0, ED, F0 or F4.
enum Pending : std::uint8_t {
  kSettled = 0,
  kOneMore,
  kTwoMore,
  kThreeMore,
  kAfterE0,
  kAfterED,
  kAfterF0,
  kAfterF4,
  kPendingCount,
  // The bytes completed the sequence: the lead did start a scalar.
  kBroken = 0xFF,
};

// How a byte stands after a lead: no continuation, or one of 80-8F, 90-9F or
// A0-BF.
enum class Follower : std::uint8_t { other, low, middle, high };

constexpr Follower follower(unsigned byte) {
  return byte < 0x80   ? Follower::other
         : byte < 0x90 ? Follower::low
         : byte < 0xA0 ? Follower::middle
         : byte < 0xC0 ? Follower::high
                       : Follower::other;
}

constexpr std::uint8_t after(std::uint8_t pending, Follower f) {
  const bool continues = f != Follower::other;
  switch (pending) {
    case kOneMore:
      return continues ? kBroken : kSettled;
    case kTwoMore:
      return continues ? kOneMore : kSettled;
    case kThreeMore:
      return continues ? kTwoMore : kSettled;
    case kAfterE0:
      return f == Follower::high ? kOneMore : kSettled;
    case kAfterED:
      return f == Follower::low || f == Follower::middle ? kOneMore : kSettled;
    case kAfterF0:
      return f == Follower::middle || f == Follower::high ? kTwoMore : kSettled;
    case kAfterF4:
      return f == Follower::low ? kTwoMore : kSettled;
    default:
      return kSettled;
  }
}

}  // namespace

std::uint32_t LexAutomatonBuilder::state() {
  if (_states.size() >= kStateLimit)
    throw Refused("the terminals are too large for one lexer");
  _states.emplace_back();
  return static_cast<std::uint32_t>(_states.size() - 1);
}

void LexAutomatonBuilder::edge(std::uint32_t from, const ByteSet& on,
                               std::uint32_t to, std::uint8_t claim) {
  _states[from].edges.push_back({on, to, claim});
}

void LexAutomatonBuilder::eps(std::uint32_t from, std::uint32_t to) {
  _states[from].eps.push_back(to);
}

void LexAutomatonBuilder::accept(std::uint32_t at, std::uint8_t back, LexTest guard,
                                 std::uint16_t waits) {
  _accepts.emplace_back(at, LexAccept{_term, back, guard, waits, _literal});
}

void LexAutomatonBuilder::test(std::uint32_t from, std::uint32_t to, std::uint32_t monitor,
                               bool unless) {
  _states[from].tests.push_back({to, monitor, unless});
  _tested = true;
}

std::uint32_t LexAutomatonBuilder::monitor(ImageId body, std::uint8_t how) {
  if (const auto known = _monitorOf.find({body, how}); known != _monitorOf.end())
    return known->second;
  // A condition that tests itself through its own conditions never ends.
  if (_depth > 8) throw NotRegular{"conditions nested too deep"};
  LexAutomatonBuilder reads(_image);
  reads._depth = _depth + 1;
  // Read wherever a character begins, it lets any characters pass before it.
  if (how == kAnywhere) {
    const Frag f = reads.char_class(kAnyChar);
    reads.eps(0, f.in);
    reads.eps(f.out, 0);
  }
  // Read whole: branches rank only a terminal's readings.
  if (!(how == kLead ? reads.add_lead(body, LexTest::none)
                     : reads.add_body(0, body, false)))
    throw NotRegular{reads._why};
  const auto at = add_monitor(reads);
  _monitorOf.emplace(std::pair(body, how), at);
  return at;
}

std::uint32_t LexAutomatonBuilder::add_monitor(const LexAutomatonBuilder& reads) {
  std::vector<std::uint32_t> start{0};
  LexDfa dfa = reads.automaton(start);
  LexMonitor made{dfa.classOf, dfa.nclasses, std::move(dfa.next), {}};
  made.flags.reserve(dfa.nstates());
  for (const auto set : dfa.acceptSet) {
    std::uint8_t flags = 0;
    for (const LexAccept& a : dfa.acceptSets[set]) {
      if (a.guard == LexTest::word) throw NotRegular{"a keyword's test inside a condition"};
      if (a.guard == LexTest::waits)
        throw NotRegular{"a condition inside a condition that reads too far"};
      flags |= (a.guard == LexTest::none ? 3 : 2) | (a.back == 0 ? 4 : 0);
    }
    made.flags.push_back(flags);
  }
  const auto at = static_cast<std::uint32_t>(_monitors.size());
  _monitors.push_back(std::move(made));
  return at;
}

std::uint32_t LexAutomatonBuilder::fold_guard(ImageId literal) {
  const auto condition = _image.children[_image.nodes[literal].first];
  const auto& guard = _image.nodes[condition];
  const bool unless = guard.kind == ImageKind::notOf;
  const auto body = unless || guard.kind == ImageKind::andOf ? _image.children[guard.first]
                                                             : condition;
  if ((needs(body) & (kCalls | kEchoes | kScanner)) == 0) {
    try {
      Waits waits;
      if (!begun(waits, {0, monitor(body), unless})) return FoldedWord::kNever;
      return waits.empty() ? FoldedWord::kPlain : wait_list(waits);
    } catch (const NotRegular&) {
    }
  }
  try {
    const auto at = program(literal);
    programs("folded literal");
    return FoldedWord::kMatcher | at;
  } catch (const NotRegular& refused) {
    throw Refused(refused.why);
  }
}

std::uint32_t LexAutomatonBuilder::pattern(ImageId body, bool anywhere) {
  if (!gap_pattern(_image, body))
    throw Refused("the grammar uses an expression in `gap::has` or `gap::is` that they cannot test: "
                  "they take classes, literals, `any`, sequences, choices and repetitions, never "
                  "a `through` or the end of input");
  try {
    _monitored = true;
    return monitor(body, anywhere ? kAnywhere : kAt);
  } catch (const NotRegular& refused) {
    throw Refused(std::string("a gap test's pattern is not regular (") + refused.why + ")");
  }
}

std::uint32_t LexAutomatonBuilder::end_monitor() {
  if (_endMonitor != ~std::uint32_t{0}) return _endMonitor;
  // Nothing read matches, and nothing more can be: only the end of input.
  LexMonitor made{{}, 1, {0, 0}, {0, 2}};
  _endMonitor = static_cast<std::uint32_t>(_monitors.size());
  _monitors.push_back(std::move(made));
  return _endMonitor;
}

std::uint32_t LexAutomatonBuilder::word_monitor() {
  if (_wordMonitor != ~std::uint32_t{0}) return _wordMonitor;
  // One character of the word class, read as a class of its own.
  LexAutomatonBuilder reads(_image);
  ImageNode word;
  word.kind = ImageKind::cpset;
  word.m = _image.bound.props;
  word.flags = _image.bound.complement ? kCpNot : std::uint8_t{0};
  word.text = _image.bound.scalars;
  const Frag f = reads.char_class(word);
  reads.accept(f.out, 0, LexTest::none);
  reads.eps(0, f.in);
  _wordMonitor = add_monitor(reads);
  return _wordMonitor;
}

std::uint16_t LexAutomatonBuilder::wait_list(const Waits& waits) {
  const auto [known, fresh] = _waitOf.try_emplace(waits, static_cast<std::uint32_t>(_waits.size()));
  if (fresh) _waits.push_back(waits);
  if (known->second >= 0xFFFF) throw NotRegular{"too many conditions left to read"};
  return static_cast<std::uint16_t>(known->second);
}

bool LexAutomatonBuilder::begun(Waits& waits, const Test& test) const {
  // A monitor that matches nothing read answers at once.
  if ((_monitors[test.monitor].flags[1] & 1) != 0) return !test.unless;
  const Wait wait{test.monitor, test.unless ? 1u : 0u, 1u};
  if (std::ranges::find(waits, wait) == waits.end()) {
    waits.push_back(wait);
    std::ranges::sort(waits);
  }
  return true;
}

bool LexAutomatonBuilder::moved(Waits& waits, unsigned byte) const {
  Waits left;
  for (const Wait& wait : waits) {
    const LexMonitor& m = _monitors[wait[0]];
    const std::uint32_t to = m.next[wait[2] * m.nclasses + m.classOf[byte]];
    const bool unless = wait[1] != 0;
    // It cannot match any more, or it just did: the test is answered.
    if (to == 0 || (m.flags[to] & 1) != 0) {
      if ((to != 0) == unless) return false;
      continue;
    }
    left.push_back({wait[0], wait[1], to});
  }
  std::ranges::sort(left);
  left.erase(std::unique(left.begin(), left.end()), left.end());
  waits = std::move(left);
  return true;
}

bool LexAutomatonBuilder::ended(const Waits& waits) const {
  return std::ranges::all_of(waits, [&](const Wait& wait) {
    return ((_monitors[wait[0]].flags[wait[2]] & 2) != 0) == (wait[1] == 0);
  });
}

void LexAutomatonBuilder::expand(std::uint32_t first, const std::vector<End>& ends,
                                 std::size_t inner, std::size_t stops) {
  // A thread of the body with tests that wait: at a state of the body, or
  // `past` bytes after end `end`.
  constexpr std::uint32_t kEnded = ~std::uint32_t{0};
  struct Thread {
    std::uint32_t at = 0;
    std::uint32_t past = 0;
    std::uint32_t end = 0;
    Waits waits;
    auto operator<=>(const Thread&) const = default;
  };
  std::map<Thread, std::uint32_t> made;
  std::vector<std::pair<Thread, std::uint32_t>> todo;
  // A state of the body no test waits beside is that state itself.
  const auto place = [&](Thread thread) {
    if (thread.waits.empty() && thread.at != kEnded) return thread.at;
    const auto [known, fresh] = made.try_emplace(thread, 0);
    if (fresh) {
      known->second = state();
      todo.emplace_back(std::move(thread), known->second);
    }
    return known->second;
  };
  // An end, as `ends` gives it, read `past` bytes past where it ends.
  const auto reached = [&](std::uint32_t end, std::uint32_t past, LexTest guard,
                         std::uint16_t waits = 0) {
    LexAccept accept = ends[end].second;
    accept.back = static_cast<std::uint8_t>(past);
    accept.guard = guard;
    accept.waits = waits;
    return accept;
  };
  // Each end, by the bytes read past it to know the tests' answer.
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> after;
  const auto end_after = [&](std::uint32_t end, std::uint32_t past) {
    const auto [known, fresh] = after.try_emplace({end, past}, 0);
    if (fresh) {
      known->second = state();
      _accepts.emplace_back(known->second, reached(end, past, LexTest::none));
    }
    return known->second;
  };
  const std::uint32_t body = static_cast<std::uint32_t>(_states.size());
  // The states where the body ends of itself: reached with no test waiting,
  // they end the token as they stand.
  std::vector<std::uint32_t> endsAlone;
  for (std::size_t i = inner; i < _accepts.size(); ++i) endsAlone.push_back(_accepts[i].first);
  std::ranges::sort(endsAlone);
  // The ends by the state they stand at, each state's in the order given.
  std::vector<std::uint32_t> endsAt(ends.size());
  for (std::uint32_t end = 0; end < ends.size(); ++end) endsAt[end] = end;
  const auto standing = [&](std::uint32_t end) { return ends[end].first; };
  std::ranges::stable_sort(endsAt, {}, standing);
  _accepts.insert(_accepts.end(), ends.begin(), ends.end());
  for (std::uint32_t q = first; q < body; ++q)
    for (std::size_t i = 0; i < _states[q].tests.size(); ++i) {
      const Test passed = _states[q].tests[i];
      Waits waits;
      if (begun(waits, passed)) eps(q, place({passed.to, 0, 0, std::move(waits)}));
    }
  while (!todo.empty()) {
    const auto [thread, at] = std::move(todo.back());
    todo.pop_back();
    // The bytes that leave the same tests waiting go on together.
    std::map<Waits, ByteSet> moves;
    const auto read = [&](const ByteSet& on) {
      moves.clear();
      for (unsigned b = 0; b < 256; ++b) {
        if (!on.has(b)) continue;
        Waits waits = thread.waits;
        if (moved(waits, b)) moves[std::move(waits)].add(b);
      }
    };
    if (thread.at == kEnded) {
      // The text past the end is read until the tests are answered: a few
      // bytes by the automaton, the rest once the end is reached.
      if (_depth == 0 && thread.past == 4) {
        _accepts.emplace_back(at, reached(thread.end, 4, LexTest::waits, wait_list(thread.waits)));
        continue;
      }
      if (ended(thread.waits))
        _accepts.emplace_back(at, reached(thread.end, thread.past, LexTest::eof));
      read(~ByteSet{});
      for (auto& [waits, on] : moves) {
        if (thread.past == 0xFF) throw NotRegular{"a condition that reads too far past the token"};
        edge(at, on,
             waits.empty() ? end_after(thread.end, thread.past + 1)
                           : place({kEnded, thread.past + 1, thread.end, waits}));
      }
      continue;
    }
    if (std::ranges::binary_search(endsAlone, thread.at))
      throw NotRegular{"an end reached while a condition waits"};
    // Its own steps: those to a state the tests made lead from a test, which
    // the thread takes below with what it waits for.
    for (std::size_t i = 0; i < _states[thread.at].eps.size(); ++i)
      if (const std::uint32_t to = _states[thread.at].eps[i]; to < body)
        eps(at, place({to, 0, 0, thread.waits}));
    for (std::size_t i = 0; i < _states[thread.at].tests.size(); ++i) {
      const Test passed = _states[thread.at].tests[i];
      Waits waits = thread.waits;
      if (begun(waits, passed)) eps(at, place({passed.to, 0, 0, std::move(waits)}));
    }
    for (std::size_t i = 0; i < _states[thread.at].edges.size(); ++i) {
      const Edge taken = _states[thread.at].edges[i];
      read(taken.on);
      for (auto& [waits, on] : moves) edge(at, on, place({taken.to, 0, 0, waits}), taken.claim);
    }
    for (const std::uint32_t end : std::ranges::equal_range(endsAt, thread.at, {}, standing)) {
      if (end >= stops)
        _accepts.emplace_back(at, ends[end].second);
      else
        eps(at, place({kEnded, 0, end, thread.waits}));
    }
  }
}

void LexAutomatonBuilder::finish(std::uint32_t first, const std::vector<End>& ends,
                                 std::size_t inner, std::size_t stops) {
  if (_tested)
    expand(first, ends, inner, stops);
  else
    _accepts.insert(_accepts.end(), ends.begin(), ends.end());
}

// Each branch of a terminal ranks as what it reads, one literal or a pattern:
// the paths of `body` that read one literal and only test around it, then
// the others, each with whether it is the literal one. A side no path takes
// is left out; the parts the two sides share are read alike.
std::vector<std::pair<ImageId, bool>> LexAutomatonBuilder::branches(ImageId body) const {
  // Per node, as reading a terminal a node calls may grow the image: its
  // literal paths and its other ones, kNoImage for none.
  std::unordered_map<ImageId, std::pair<ImageId, ImageId>> sides;
  std::unordered_set<ImageId> begun;
  const auto tests = [&](ImageId id) {
    const ImageKind kind = _image.nodes[_image.bare(id)].kind;
    return kind == ImageKind::notOf || kind == ImageKind::andOf || kind == ImageKind::eof;
  };
  // The one part of a sequence that reads, none where several do.
  const auto reader = [&](const ImageNode& n) {
    std::uint32_t at = n.count;
    for (std::uint32_t i = 0; i < n.count; ++i) {
      if (tests(_image.children[n.first + i])) continue;
      if (at != n.count) return n.count;
      at = i;
    }
    return at;
  };
  std::vector<std::pair<ImageId, bool>> open{{body, false}};
  std::vector<ImageId> parts, literal, other;
  while (!open.empty()) {
    const auto [id, ready] = open.back();
    const ImageId at = _image.bare(id);
    if (sides.contains(at)) {
      open.pop_back();
      continue;
    }
    const ImageNode n = _image.nodes[at];
    const std::uint32_t read = n.kind == ImageKind::seq ? reader(n) : n.count;
    if (!ready) {
      // Its parts first; met again among them, it is read whole.
      if (!begun.insert(at).second) {
        sides.emplace(at, std::pair{kNoImage, at});
        open.pop_back();
        continue;
      }
      open.back().second = true;
      if (n.kind == ImageKind::choice)
        for (std::uint32_t i = 0; i < n.count; ++i) open.emplace_back(_image.children[n.first + i], false);
      else if (n.kind == ImageKind::seq && read != n.count)
        open.emplace_back(_image.children[n.first + read], false);
      continue;
    }
    open.pop_back();
    std::pair<ImageId, ImageId> side{kNoImage, at};
    if (n.kind == ImageKind::lit) {
      side = {at, kNoImage};
    } else if (n.kind == ImageKind::choice) {
      literal.clear();
      other.clear();
      for (std::uint32_t i = 0; i < n.count; ++i) {
        const auto [l, o] = sides.at(_image.bare(_image.children[n.first + i]));
        if (l != kNoImage) literal.push_back(l);
        if (o != kNoImage) other.push_back(o);
      }
      const auto made = [&](const std::vector<ImageId>& arms) {
        if (arms.empty()) return kNoImage;
        if (arms.size() == 1) return arms.front();
        return arms.size() == n.count && std::equal(arms.begin(), arms.end(),
                                                     _image.children.begin() + n.first,
                                                     [&](ImageId a, ImageId c) { return a == _image.bare(c); })
                   ? at
                   : _image.group(ImageKind::choice, arms);
      };
      side = {made(literal), made(other)};
    } else if (n.kind == ImageKind::seq && read != n.count) {
      const ImageId part = _image.bare(_image.children[n.first + read]);
      const auto [l, o] = sides.at(part);
      const auto made = [&](ImageId with) {
        if (with == kNoImage) return kNoImage;
        if (with == part) return at;
        parts.assign(_image.children.begin() + n.first, _image.children.begin() + n.first + n.count);
        parts[read] = with;
        return _image.group(ImageKind::seq, parts);
      };
      side = {made(l), made(o)};
    }
    sides.emplace(at, side);
  }
  const auto [l, o] = sides.at(_image.bare(body));
  std::vector<std::pair<ImageId, bool>> out;
  if (l != kNoImage) out.emplace_back(l, true);
  if (o != kNoImage) out.emplace_back(o, false);
  return out;
}

void LexAutomatonBuilder::add(std::uint16_t term, const Terminal& terminal) {
  const auto body = terminal.body;
  if (body == kNoImage) return;
  const std::size_t states = _states.size();
  const std::size_t accepts = _accepts.size();
  const std::size_t entries = _states[0].eps.size();
  _unboundedTest = false;
  try {
    const auto arms = branches(body);
    for (const auto& [id, literal] : arms) {
      _literal = literal;
      if (add_body(term, id, true)) continue;
      _states.resize(states);
      _accepts.resize(accepts);
      _states[0].eps.resize(entries);
      for (const auto& [branch, ranked] : arms) {
        _literal = ranked;
        if (!add_lead(branch, LexTest::match)) throw NotRegular{_why};
      }
      _literal = false;
      programs(terminal.name);
      return;
    }
    const auto root = _image.nodes[_image.bare(body)];
    const bool openRun = root.kind == ImageKind::repeat && root.n == kUnboundedCount &&
                         (_image.nodes[_image.children[root.first]].kind == ImageKind::seq ||
                          _image.nodes[_image.children[root.first]].kind == ImageKind::choice);
    if ((openRun || _unboundedTest) &&
        std::ranges::none_of(arms, [](const auto& arm) { return arm.second; }))
      consider(states, accepts, entries, terminal.trivia == Trivia::ignored);
  } catch (const NotRegular& refused) {
    throw Refused(terminal.name + " cannot be read (" + refused.why + ")");
  }
}

// Direct repetitions have disjoint first bytes. An ignored repetition takes
// precedence so ordinary token modes can skip it in the main DFA.
void LexAutomatonBuilder::consider(std::size_t first, std::size_t ends, std::size_t root, bool ignored) {
  const auto endings = std::span(_accepts).subspan(ends);
  if (endings.empty() || std::ranges::any_of(endings, [](const End& e) {
        return e.second.guard == LexTest::word;
      }))
    return;
  Run run{{}, root, ends, _accepts.size(), false, ignored};
  std::vector<bool> seen(_states.size() - first);
  std::vector<std::uint32_t> work{_states[0].eps[root]};
  while (!work.empty()) {
    const auto q = work.back();
    work.pop_back();
    if (seen[q - first]) continue;
    seen[q - first] = true;
    for (const Edge& e : _states[q].edges) run.bytes |= e.on;
    for (const auto to : _states[q].eps) work.push_back(to);
  }
  run.nullable = std::ranges::any_of(endings, [&](const End& e) { return seen[e.first - first]; });
  if (_unboundedTest) {
    factor(run);
    return;
  }
  if (!run.bytes.any()) return;
  const auto overlaps = [&](const ByteSet& other) {
    for (std::size_t i = 0; i < run.bytes.words.size(); ++i)
      if (run.bytes.words[i] & other.words[i]) return true;
    return false;
  };
  const bool shared = overlaps(_loopLeads);
  const bool keep = !shared || (ignored && !overlaps(_ignoredLeads));
  if (shared)
    std::erase_if(_isolated, [&](const Run& prior) {
      if ((!ignored && prior.ignored) || !overlaps(prior.bytes)) return false;
      factor(prior);
      return true;
    });
  _loopLeads |= run.bytes;
  if (ignored) _ignoredLeads |= run.bytes;
  if (keep) _isolated.push_back(run);
  else factor(run);
}

// The automaton reads the run's first byte and the matcher the whole body, as
// one automaton: it weighs every end of a state, whatever its guard.
void LexAutomatonBuilder::factor(const Run& run) {
  const auto endings = std::span(_accepts).subspan(run.ends, run.limit - run.ends);
  const auto term = endings.front().second.term;
  for (auto& [q, action] : endings) action.term = 0;
  const auto body = static_cast<std::uint16_t>(entry(_states[0].eps[run.root]));
  const LexAccept match{term, 0, LexTest::match, body};
  const auto lead = state();
  const auto start = state();
  _states[0].eps[run.root] = start;
  edge(start, run.bytes, lead);
  _accepts.emplace_back(lead, match);
  if (run.nullable) _accepts.emplace_back(0, match);
}

void LexAutomatonBuilder::begin_body() {
  _tested = false;
  for (const std::uint32_t rule : _inlining) _inlined[rule] = false;
  _inlining.clear();
}

bool LexAutomatonBuilder::add_body(std::uint16_t term, ImageId body, bool tail) {
  const std::size_t states = _states.size();
  const std::size_t accepts = _accepts.size();
  _term = term;
  for (;;) {
    begin_body();
    try {
      const Frag f = detail::descend(compile(body, tail));
      finish(static_cast<std::uint32_t>(states),
             {{f.out, LexAccept{_term, 0, LexTest::none, 0, _literal}}}, accepts, 1);
      eps(0, f.in);
      return true;
    } catch (const NotRegular& refused) {
      _why = refused.why;
      _states.resize(states);
      _accepts.resize(accepts);
      // A keyword that ends the body is told from a word by a test of the
      // lexer's, which waits for no condition: it is read as a condition too.
      if (!tail || !_tested) return false;
      tail = false;
    }
  }
}

bool LexAutomatonBuilder::add_lead(ImageId body, LexTest guard) {
  const std::size_t states = _states.size();
  const std::size_t accepts = _accepts.size();
  begin_body();
  _leads.clear();
  _passed = false;
  try {
    // A scanner's prefilter stops at the first iteration of an unbounded prefix.
    const std::uint8_t stops = kCalls | kEchoes | kScanner | ((needs(body) & kScanner) ? kLoop : 0);
    const Frag f = detail::descend(lead(body, stops));
    // A lead ends past its first byte: a stop where it begins is dropped,
    // unless it is the only one.
    std::set<std::uint32_t> empty;
    for (std::vector<std::uint32_t> open{f.in}; !open.empty();) {
      const std::uint32_t q = open.back();
      open.pop_back();
      if (!empty.insert(q).second) continue;
      open.insert(open.end(), _states[q].eps.begin(), _states[q].eps.end());
      for (const Test& passed : _states[q].tests) open.push_back(passed.to);
    }
    const auto begins = [&](std::uint32_t at) { return empty.contains(at); };
    if (!std::ranges::all_of(_leads, begins) || (f.out != kStopped && !begins(f.out)))
      std::erase_if(_leads, begins);
    // Where a terminal's lead read the whole body and passed no condition,
    // the token ends there as the automaton read it.
    std::vector<End> ends;
    if (guard == LexTest::match && !_passed && f.out != kStopped)
      ends.emplace_back(f.out, LexAccept{_term, 0, LexTest::none, 0, _literal});
    else
      stop(f.out);
    const std::size_t whole = ends.size();
    const auto entry = guard == LexTest::match && !_leads.empty()
                           ? static_cast<std::uint16_t>(program(body))
                           : std::uint16_t{0};
    for (const auto at : _leads) ends.emplace_back(at, LexAccept{_term, 0, guard, entry, _literal});
    finish(static_cast<std::uint32_t>(states), ends, accepts, whole);
    eps(0, f.in);
    return true;
  } catch (const NotRegular& refused) {
    _why = refused.why;
    _states.resize(states);
    _accepts.resize(accepts);
    return false;
  }
}

std::uint32_t LexAutomatonBuilder::entry(std::uint32_t state) {
  const auto [it, fresh] = _entryOf.try_emplace(state, static_cast<std::uint32_t>(_entries.size()));
  if (fresh) {
    if (_entries.size() >= 0xFFFF) throw NotRegular{"too many places for the matcher to read from"};
    _entries.push_back(state);
  }
  return it->second;
}

std::uint32_t LexAutomatonBuilder::program(ImageId body, std::uint32_t family, std::uint64_t least,
                                           std::uint64_t most) {
  const auto [known, fresh] = _programOf.try_emplace({body, family, least, most}, 0);
  if (!fresh) return known->second;
  const std::uint32_t in = state();
  known->second = entry(in);
  _toRead.push_back({body, in, family == kOwnFamily ? known->second : family, least, most});
  return known->second;
}

std::uint32_t LexAutomatonBuilder::tag(std::string_view name) {
  const auto [it, fresh] = _tags.try_emplace(std::pair(_family, name), _tagCount[_family]);
  if (fresh && ++_tagCount[_family] > kMatchTags)
    throw NotRegular{"more than four tags of marks in one terminal"};
  return it->second;
}

void LexAutomatonBuilder::exit(std::uint32_t at, MatchAction action, std::uint32_t arg,
                               std::uint32_t to) {
  std::uint32_t id = 0;
  if (action != MatchAction::end) {
    if (_exits.size() >= 0xFFFF) throw NotRegular{"too many exits for the matcher"};
    id = static_cast<std::uint32_t>(_exits.size());
    _exits.push_back({action, static_cast<std::uint16_t>(entry(to)), arg});
  }
  _exitAt.emplace_back(at, LexAccept{static_cast<std::uint16_t>(id), 0, LexTest::none});
}

void LexAutomatonBuilder::programs(std::string_view name) {
  struct Marks {
    std::uint64_t required = 0;
    bool own = false;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> callers;
  };
  std::map<std::uint32_t, Marks> marks;
  std::vector<std::uint32_t> pending;
  _matching = true;
  while (!_toRead.empty()) {
    const ToRead read = _toRead.back();
    const std::uint32_t in = read.in;
    _toRead.pop_back();
    _family = read.family;
    const std::size_t states = _states.size();
    const std::size_t accepts = _accepts.size();
    begin_body();
    _exitAt.clear();
    const Frag f = detail::descend(read.most == 0 ? compile(read.body, false)
                                                  : copies(read.body, read.least, read.most));
    eps(in, f.in);
    const std::uint32_t ends = state();
    eps(f.out, ends);
    exit(ends, MatchAction::end, 0, 0);
    if (!(needs(read.body) & kEchoes)) {
      finish(static_cast<std::uint32_t>(states), _exitAt, accepts, _exitAt.size());
      continue;
    }
    const auto entry = _entryOf.at(in);
    Marks& local = marks[entry];
    local.own = read.family == entry;
    std::map<std::uint32_t, std::uint32_t> exitOf;
    for (const End& e : _exitAt) exitOf.emplace(e.first, e.second.term);
    // Marks set on every local path cover echoes here and in conditions.
    std::map<std::uint32_t, std::uint64_t> held{{in, 0}};
    std::vector<std::uint32_t> work{in};
    const auto reach = [&](std::uint32_t q, std::uint64_t tags) {
      const auto [it, fresh] = held.try_emplace(q, tags);
      if (fresh || (it->second & tags) != it->second) {
        it->second &= tags;
        work.push_back(q);
      }
    };
    while (!work.empty()) {
      const std::uint32_t q = work.back();
      work.pop_back();
      const std::uint64_t tags = held[q];
      for (const Edge& e : _states[q].edges) reach(e.to, tags);
      for (const std::uint32_t to : _states[q].eps) reach(to, tags);
      for (const Test& t : _states[q].tests) reach(t.to, tags);
      const auto exited = exitOf.find(q);
      if (exited == exitOf.end() || exited->second == 0) continue;
      const MatchExit& e = _exits[exited->second];
      std::uint64_t after = tags;
      if (e.action == MatchAction::echo) local.required |= (std::uint64_t{1} << e.arg) & ~tags;
      if (e.action == MatchAction::close) after |= std::uint64_t{1} << e.arg;
      if (e.action == MatchAction::ahead || e.action == MatchAction::unless)
        marks[e.arg].callers.emplace_back(entry, tags);
      reach(_entries[e.to], after);
    }
    if (local.required) pending.push_back(entry);
    finish(static_cast<std::uint32_t>(states), _exitAt, accepts, _exitAt.size());
  }
  _matching = false;
  // A condition inherits a missing mark from every caller. A terminal starts empty.
  while (!pending.empty()) {
    const Marks& body = marks.at(pending.back());
    pending.pop_back();
    if (body.own)
      throw Refused("terminal `" + std::string(name) +
                    "` uses `same<>` where a `mark<>` with its tag is not set on every path "
                    "before it");
    for (const auto& [caller, held] : body.callers) {
      auto& required = marks.at(caller).required;
      if (const auto missing = body.required & ~held & ~required) {
        required |= missing;
        pending.push_back(caller);
      }
    }
  }
}

std::uint8_t LexAutomatonBuilder::needs(ImageId root) {
  constexpr std::uint32_t kReady = 1u << 31;
  if (const auto it = _needs.find(root); it != _needs.end())
    return static_cast<std::uint8_t>(it->second);

  struct Node {
    ImageId id;
    std::uint8_t flags = 0;
  };
  std::vector<Node> nodes{{root}};
  std::vector<std::uint32_t> starts, edges;
  _needs.emplace(root, 0);
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    starts.push_back(static_cast<std::uint32_t>(edges.size()));
    const ImageNode n = _image.nodes[nodes[i].id];
    std::uint8_t flags = 0;
    if (n.kind == ImageKind::mark) flags = kMarks;
    if (n.kind == ImageKind::same) flags = kEchoes;
    if (n.kind == ImageKind::scan) flags = kScanner;
    if (n.kind == ImageKind::through || (n.kind == ImageKind::repeat && n.n == kUnboundedCount))
      flags = kLoop;
    const auto child = [&](ImageId id) {
      const auto [it, fresh] = _needs.try_emplace(id, static_cast<std::uint32_t>(nodes.size()));
      if (fresh) nodes.push_back({id});
      if (it->second & kReady) flags |= static_cast<std::uint8_t>(it->second);
      else edges.push_back(it->second);
    };
    if (n.kind == ImageKind::ref) {
      const auto& stage = _image.stage(n.a);
      if (stage.kind == ImageRuleKind::terminal) child(_image.node(*stage.body, _image.folds(n.a)));
    } else {
      for (std::uint32_t k = 0; k < n.count; ++k) child(_image.children[n.first + k]);
    }
    nodes[i].flags = flags;
  }
  starts.push_back(static_cast<std::uint32_t>(edges.size()));

  strong_components(starts, edges, [&](auto group) {
    std::uint8_t flags = group.size() > 1 ? kCalls : 0;
    for (const auto v : group) {
      flags |= nodes[v].flags;
      for (auto k = starts[v]; k < starts[v + 1]; ++k)
        flags |= edges[k] == v ? std::uint8_t{kCalls} : nodes[edges[k]].flags;
    }
    for (const auto v : group) {
      nodes[v].flags = flags;
      _needs.at(nodes[v].id) = kReady | flags;
    }
  });
  return nodes.front().flags;
}

detail::Descent<std::optional<LexAutomatonBuilder::Frag>> LexAutomatonBuilder::attempt(
    ImageId id) {
  const std::size_t states = _states.size();
  const std::size_t accepts = _accepts.size();
  const std::size_t inlining = _inlining.size();
  try {
    co_return co_await compile(id, false);
  } catch (const NotRegular&) {
    _states.resize(states);
    _accepts.resize(accepts);
    for (; _inlining.size() > inlining; _inlining.pop_back()) _inlined[_inlining.back()] = false;
  }
  co_return std::nullopt;
}

void LexAutomatonBuilder::stop(std::uint32_t at) {
  if (at != kStopped) _leads.push_back(at);
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::lead(ImageId id, std::uint8_t stops) {
  // What the automaton reads alone is read as it stands.
  if ((needs(id) & stops) == 0)
    if (const std::optional<Frag> f = co_await attempt(id)) co_return *f;
  const ImageNode n = _image.nodes[id];
  switch (n.kind) {
    case ImageKind::scan: {
      // A successful scan needs one byte. The matcher restarts at the token head.
      const Frag f{state(), state()};
      edge(f.in, ~ByteSet{}, f.out);
      stop(f.out);
      co_return Frag{f.in, kStopped};
    }
    case ImageKind::doc:
    case ImageKind::mark:
      co_return co_await lead(_image.children[n.first], stops);
    case ImageKind::notOf:
    case ImageKind::andOf: {
      // The matcher reads the condition: the lead passes it.
      _passed = true;
      const std::uint32_t at = state();
      co_return Frag{at, at};
    }
    case ImageKind::lit:
      // Its guard is the matcher's.
      _passed = true;
      co_return literal(n, false);
    case ImageKind::seq: {
      Frag all{state(), 0};
      all.out = all.in;
      for (std::uint32_t i = 0; i < n.count && all.out != kStopped; ++i) {
        const Frag part = co_await lead(_image.children[n.first + i], stops);
        eps(all.out, part.in);
        all.out = part.out;
      }
      co_return all;
    }
    case ImageKind::choice: {
      Frag all{state(), kStopped};
      for (std::uint32_t i = 0; i < n.count; ++i) {
        const Frag arm = co_await lead(_image.children[n.first + i], stops);
        eps(all.in, arm.in);
        if (arm.out == kStopped) continue;
        if (all.out == kStopped) all.out = state();
        eps(arm.out, all.out);
      }
      co_return all;
    }
    case ImageKind::repeat: {
      // A first copy leads; the matcher reads the others. One read at most
      // leads on past it.
      const Frag item = co_await lead(_image.children[n.first], stops);
      if (n.n == 1 && n.m == 1) co_return item;
      const Frag all{state(), state()};
      eps(all.in, item.in);
      if (n.n != 1) {
        // A loop is the matcher's from where it begins.
        stop(item.out);
        stop(all.in);
      } else if (item.out != kStopped) {
        eps(item.out, all.out);
      }
      if (n.m != 0) co_return Frag{all.in, kStopped};
      eps(all.in, all.out);
      co_return all;
    }
    case ImageKind::through: {
      const Frag open = co_await lead(_image.children[n.first], stops);
      stop(open.out);
      co_return Frag{open.in, kStopped};
    }
    case ImageKind::ref: {
      const ImageStageDesc& stage = _image.stage(n.a);
      if (stage.kind != ImageRuleKind::terminal) throw NotRegular{"a call to a rule"};
      if (n.a >= _inlined.size()) _inlined.resize(n.a + 1, false);
      if (!_inlined[n.a]) {
        _inlined[n.a] = true;
        _inlining.push_back(n.a);
        const Frag f = co_await lead(_image.bare(id), stops);
        _inlining.pop_back();
        _inlined[n.a] = false;
        co_return f;
      }
      // A terminal met again on the way to itself ends the lead there.
      [[fallthrough]];
    }
    default: {
      const std::uint32_t at = state();
      stop(at);
      co_return Frag{at, kStopped};
    }
  }
}

void LexAutomatonBuilder::exit_ahead(const Frag& f, ImageId body, MatchAction action,
                                     std::uint32_t arg, bool unless) {
  // Where no monitor reads those bytes, the exit is taken wherever it stands.
  std::uint32_t at = f.in;
  try {
    const std::uint32_t led = monitor(body, kLead);
    at = state();
    test(f.in, at, led, false);
    if (unless) test(f.in, f.out, led, true);
  } catch (const NotRegular&) {
  }
  exit(at, action, arg, f.out);
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::condition(ImageId body,
                                                                         bool unless) {
  const Frag f{state(), state()};
  const std::uint8_t needed = needs(body);
  _unboundedTest = _unboundedTest || (needed & kLoop);
  if (!_matching && (needed & kScanner) != 0)
    throw NotRegular{"a condition with unbounded lookahead"};
  if (!_matching || (needed & (kCalls | kEchoes | kScanner)) == 0) {
    try {
      test(f.in, f.out, monitor(body), unless);
      co_return f;
    } catch (const NotRegular&) {
      if (!_matching) throw;
    }
  }
  exit_ahead(f, body, unless ? MatchAction::unless : MatchAction::ahead, program(body, _family),
             unless);
  co_return f;
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::compile(
    ImageId id, bool tail) {
  // A copy: reading a rule's body may grow the image under the reference.
  const ImageNode n = _image.nodes[id];
  switch (n.kind) {
    case ImageKind::epsilon: {
      const std::uint32_t s = state();
      co_return Frag{s, s};
    }
    case ImageKind::lit: {
      Frag f = literal(n, tail);
      if ((n.flags & kLitGuardExpr) != 0) {
        const ImageNode guardNode = _image.nodes[_image.children[n.first]];
        const bool predicate = guardNode.kind == ImageKind::andOf || guardNode.kind == ImageKind::notOf;
        const ImageId body = predicate ? _image.children[guardNode.first] : _image.children[n.first];
        if (tail && (n.flags & kLitKeyword) != 0) {
          // Elect the keyword at its last byte, even where trivia reads further.
          accept(f.out, 0, LexTest::waits, wait_list({{monitor(body), 1, 1}}));
          f.out = state();
          co_return f;
        }
        const Frag guard = co_await condition(body, guardNode.kind == ImageKind::notOf);
        eps(f.out, guard.in);
        f.out = guard.out;
      }
      co_return f;
    }
    case ImageKind::andOf:
    case ImageKind::notOf:
      co_return co_await condition(_image.children[n.first], n.kind == ImageKind::notOf);
    case ImageKind::mark: {
      // Read alone, a mark is what it reads; the matcher notes where it began
      // and ended.
      if (!_matching) co_return co_await compile(_image.children[n.first], tail);
      const std::uint32_t marked = tag(n.text2);
      const Frag body = co_await compile(_image.children[n.first], false);
      const Frag f{state(), state()};
      exit(f.in, MatchAction::open, marked, body.in);
      const std::uint32_t closed = state();
      eps(body.out, closed);
      exit(closed, MatchAction::close, marked, f.out);
      co_return f;
    }
    case ImageKind::scan: {
      if (!_matching) throw NotRegular{"a scanner callback"};
      const Frag f{state(), state()};
      exit(f.in, MatchAction::scan, static_cast<std::uint32_t>(n.m), f.out);
      co_return f;
    }
    case ImageKind::same: {
      if (!_matching) throw NotRegular{"an echo of a mark"};
      const Frag f{state(), state()};
      exit(f.in, MatchAction::echo, tag(n.text2), f.out);
      co_return f;
    }
    case ImageKind::eof: {
      const Frag f{state(), state()};
      test(f.in, f.out, end_monitor(), false);
      co_return f;
    }
    case ImageKind::cpset:
      co_return char_class(n);
    case ImageKind::byte: {
      const Frag f{state(), state()};
      edge(f.in, ~ByteSet{}, f.out);
      co_return f;
    }
    case ImageKind::seq:
      co_return co_await parts(n, tail);
    case ImageKind::choice:
      co_return co_await choice(n, tail);
    case ImageKind::repeat:
      co_return co_await copies(_image.children[n.first], n.m, n.n);
    case ImageKind::doc:
      co_return co_await compile(_image.children[n.first], tail);
    case ImageKind::through:
      co_return co_await through(n, tail);
    case ImageKind::ref: {
      const ImageStageDesc& stage = _image.stage(n.a);
      if (stage.kind != ImageRuleKind::terminal) throw NotRegular{"a call to a rule"};
      const ImageId body = _image.node(*stage.body, _image.folds(n.a));
      // The matcher calls a terminal that reaches itself, echoes or marks,
      // where its first bytes stand: it starts with no mark, and its own are
      // gone once it ends.
      if (_matching) {
        const std::uint8_t needed = needs(id);
        if ((needed & (kCalls | kEchoes | kMarks)) != 0) {
          const Frag f{state(), state()};
          exit_ahead(f, body, MatchAction::call, program(body), false);
          co_return f;
        }
      }
      if (n.a >= _inlined.size()) _inlined.resize(n.a + 1, false);
      if (_inlined[n.a]) throw NotRegular{"a terminal that reads itself"};
      _inlined[n.a] = true;
      _inlining.push_back(n.a);
      const Frag f = co_await compile(_image.bare(id), tail);
      _inlining.pop_back();
      _inlined[n.a] = false;
      co_return f;
    }
    case ImageKind::embed:
      throw Refused("`section<G>(open, close)` is allowed in a rule, not in a terminal");
    default:
      throw NotRegular{"a construct only a rule reads"};
  }
}

LexAutomatonBuilder::Frag LexAutomatonBuilder::literal(const ImageNode& n,
                                                       bool tail) {
  const bool word = word_guarded(_image, n);
  const bool folded = (n.flags & kLitFolded) != 0;
  Frag f{state(), 0};
  std::uint32_t at = f.in;
  for (std::size_t i = 0; i < n.text.size();) {
    const auto c = static_cast<unsigned char>(n.text[i]);
    std::size_t length = 0;
    const char32_t cp =
        folded && c >= 0x80 ? scalarAt(n.text.data() + i, n.text.size() - i, length) : kNoScalar;
    const std::uint32_t next = state();
    if (cp == kNoScalar) {
      ByteSet on = ByteSet::of(c);
      if (folded && ascii_letter(c)) on.add(c ^ 0x20u);
      edge(at, on, next);
      length = 1;
    } else {
      // A letter past ASCII is read in each of its cases.
      const auto spell = [&](const ByteRange* seq, unsigned count) {
        std::uint32_t from = at;
        for (unsigned k = 0; k < count; ++k) {
          const std::uint32_t to = k + 1 == count ? next : state();
          edge(from, ByteSet::range(seq[k].lo, seq[k].hi), to);
          from = to;
        }
      };
      utf8_sequences(cp, cp, spell);
      cursiv::detail::for_each_other_case(cp, [&](char32_t other) { utf8_sequences(other, other, spell); });
    }
    at = next;
    i += length;
  }
  if (!word) {
    f.out = at;
    return f;
  }
  if (!tail) {
    // A keyword inside a body is read where no word goes on past it.
    f.out = state();
    test(at, f.out, word_monitor(), true);
    return f;
  }
  accept(at, 0, LexTest::word);
  f.out = state();
  return f;
}

LexAutomatonBuilder::Frag LexAutomatonBuilder::char_class(const ImageNode& n) {
  const CharClass cls =
      CharClass::of(n.m, (n.flags & kCpNot) != 0, n.text);
  if (cls.ranges.empty() && !cls.invalid)
    throw Refused("a character class matches no character");
  const Frag f{state(), state()};
  // Sequences of one length share their continuation states where the ranges
  // after the lead byte agree.
  std::map<std::vector<std::pair<std::uint8_t, std::uint8_t>>, std::uint32_t> tails;
  // The lead bytes by what follows them: one edge per continuation.
  std::map<std::uint32_t, ByteSet> leads;
  auto emit = [&](const ByteRange* seq, unsigned length) {
    // The state reading `seq[k..]`, made once per distinct suffix.
    std::uint32_t to = f.out;
    for (unsigned k = length; k-- > 1;) {
      std::vector<std::pair<std::uint8_t, std::uint8_t>> key;
      for (unsigned j = k; j < length; ++j) key.emplace_back(seq[j].lo, seq[j].hi);
      const auto [it, fresh] = tails.try_emplace(std::move(key), 0u);
      if (fresh) {
        it->second = state();
        edge(it->second, ByteSet::range(seq[k].lo, seq[k].hi), to);
      }
      to = it->second;
    }
    leads[to] |= ByteSet::range(seq[0].lo, seq[0].hi);
  };
  for (const unicode::Range r : cls.ranges) utf8_sequences(r.lo, r.hi, emit);
  if (cls.invalid) {
    // A byte that can start no scalar is read as it stands; a lead, where
    // what follows does not complete its sequence.
    leads[f.out] |= ByteSet::range(0x80, 0xC1);
    leads[f.out] |= ByteSet::range(0xF5, 0xFF);
    ByteSet three = ByteSet::range(0xE1, 0xEC);
    three |= ByteSet::range(0xEE, 0xEF);
    edge(f.in, ByteSet::range(0xC2, 0xDF), f.out, kOneMore);
    edge(f.in, ByteSet::of(0xE0), f.out, kAfterE0);
    edge(f.in, three, f.out, kTwoMore);
    edge(f.in, ByteSet::of(0xED), f.out, kAfterED);
    edge(f.in, ByteSet::of(0xF0), f.out, kAfterF0);
    edge(f.in, ByteSet::range(0xF1, 0xF3), f.out, kThreeMore);
    edge(f.in, ByteSet::of(0xF4), f.out, kAfterF4);
  }
  for (const auto& [to, on] : leads) edge(f.in, on, to);
  return f;
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::parts(ImageNode n, bool tail) {
  if (!n.count) {
    const std::uint32_t s = state();
    co_return Frag{s, s};
  }
  Frag all = co_await compile(_image.children[n.first], tail && n.count == 1);
  for (std::uint32_t i = 1; i < n.count; ++i) {
    const Frag part = co_await compile(_image.children[n.first + i], tail && i + 1 == n.count);
    eps(all.out, part.in);
    all.out = part.out;
  }
  co_return all;
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::choice(
    ImageNode n, bool tail) {
  const Frag all{state(), state()};
  for (std::uint32_t i = 0; i < n.count; ++i) {
    const Frag part = co_await compile(_image.children[n.first + i], tail);
    eps(all.in, part.in);
    eps(part.out, all.out);
  }
  co_return all;
}

detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::copies(
    ImageId item, std::uint64_t least, std::uint64_t most) {
  // Past the copies an automaton unrolls, the matcher reads them: those asked
  // for, then the others; all asked for or none, by two calls of half as
  // many, then the odd one. A call starts with no mark.
  if ((most == kUnboundedCount ? least + 1 : most) > kUnrolled) {
    if (!_matching) throw NotRegular{"a repetition counted past what the lexer unrolls"};
    if ((needs(item) & (kEchoes | kMarks)) != 0)
      throw NotRegular{"a mark in a repetition counted past what the lexer unrolls"};
    const std::uint32_t in = state();
    std::uint32_t at = in;
    const auto join = [&](const Frag& part) {
      eps(at, part.in);
      at = part.out;
    };
    if (most == kUnboundedCount || (least != 0 && least != most)) {
      join(co_await copies(item, least, least));
      join(co_await copies(item, 0, most == kUnboundedCount ? most : most - least));
      co_return Frag{in, at};
    }
    for (int i = 0; i < 2; ++i) {
      const Frag half{at, state()};
      exit_ahead(half, item, MatchAction::call, program(item, kOwnFamily, least / 2, most / 2),
                 false);
      if (least == 0) eps(half.in, half.out);
      at = half.out;
    }
    if (most % 2 != 0) join(co_await copies(item, least % 2, 1));
    co_return Frag{in, at};
  }
  const std::uint32_t in = state();
  std::uint32_t at = in;
  Frag last{};
  for (std::uint64_t i = 0; i < least; ++i) {
    last = co_await compile(item, false);
    eps(at, last.in);
    at = last.out;
  }
  if (most == kUnboundedCount) {
    // The last copy is read again; with none asked for, one that may be
    // passed over.
    if (least == 0) {
      last = co_await compile(item, false);
      eps(at, last.in);
      eps(at, last.out);
      at = last.out;
    }
    eps(at, last.in);
    co_return Frag{in, at};
  }
  const std::uint32_t out = state();
  for (std::uint64_t i = least; i < most; ++i) {
    const Frag part = co_await compile(item, false);
    eps(at, out);
    eps(at, part.in);
    at = part.out;
  }
  eps(at, out);
  co_return Frag{in, out};
}

// The literals that close a `through`, in written order: each read, or only
// looked at under `&`; and whether the end of input closes it too.
void LexAutomatonBuilder::closers(ImageId close, std::vector<Closer>& out,
                                  bool& eof) const {
  std::vector<std::pair<ImageId, bool>> todo{{close, true}};
  // A node met again, as through a terminal that names itself, adds nothing.
  std::set<std::pair<ImageId, bool>> seen;
  while (!todo.empty()) {
    const auto [id, consumed] = todo.back();
    todo.pop_back();
    const ImageId at = _image.bare(id);
    if (!seen.emplace(at, consumed).second) continue;
    const ImageNode& n = _image.nodes[at];
    switch (n.kind) {
      case ImageKind::lit:
        // The text is read a character at a time: only a closer made of
        // whole characters closes where the bytes show it.
        if ((n.flags & ~kLitKeyword) != 0 || !detail::utf8_well_formed(n.text))
          throw NotRegular{"a closer with a guard or in either case"};
        out.push_back({std::string(n.text), consumed});
        break;
      case ImageKind::eof:
        eof = true;
        break;
      case ImageKind::andOf:
        if (!consumed) throw NotRegular{"a condition inside a closer's condition"};
        todo.emplace_back(_image.children[n.first], false);
        break;
      case ImageKind::choice:
        for (std::uint32_t i = n.count; i-- > 0;)
          todo.emplace_back(_image.children[n.first + i], consumed);
        break;
      default:
        throw NotRegular{"a closer that is no literal"};
    }
  }
}

// `open`, then the text up to the first place a closer stands, read by the
// closers' own matching automaton: a state is the longest end of the text that
// begins a closer.
detail::Descent<LexAutomatonBuilder::Frag> LexAutomatonBuilder::through(
    ImageNode n, bool tail) {
  std::vector<Closer> ends;
  bool eof = false;
  bool literal = true;
  try {
    closers(_image.children[n.first + 1], ends, eof);
  } catch (const NotRegular&) {
    literal = false;
  }
  if (literal && ends.empty() && !eof) throw NotRegular{"a run with no closer"};
  const bool plain = !eof && std::ranges::all_of(ends, &Closer::consumed);
  // The closers' trie; node 0 is the empty text.
  struct Node {
    std::array<std::int32_t, 256> child;
    int back = -1;
    std::uint32_t state = 0;
    Node() { child.fill(-1); }
  };
  std::vector<Node> trie(1);
  // Where each byte leads from each node: its child, or where the longest
  // shorter end of the text leads. Nodes are visited by depth, so that end's
  // row is already whole.
  std::vector<std::array<std::uint32_t, 256>> go;
  // Whether the trie reads the closers. It gives back a closer that is not
  // read by its length, at most 255 bytes, and cannot tell which closes where
  // one ends inside another that is still being read.
  const auto trie_reads = [&] {
    for (const Closer& end : ends) {
      if (!end.consumed && end.bytes.size() > 0xFF) return false;
      std::uint32_t at = 0;
      for (const char ch : end.bytes) {
        const auto b = static_cast<unsigned char>(ch);
        if (trie[at].child[b] < 0) {
          trie[at].child[b] = static_cast<std::int32_t>(trie.size());
          trie.emplace_back();
        }
        at = static_cast<std::uint32_t>(trie[at].child[b]);
      }
      if (end.consumed || trie[at].back < 0)
        trie[at].back = end.consumed ? 0 : static_cast<int>(end.bytes.size());
    }
    go.resize(trie.size());
    std::vector<std::uint32_t> fail(trie.size(), 0), order{0};
    // Per node, whether a closer ends its text without beginning it.
    std::vector<bool> closesWithin(trie.size(), false);
    for (std::size_t at = 0; at < order.size(); ++at) {
      const std::uint32_t u = order[at];
      for (unsigned b = 0; b < 256; ++b) {
        const std::int32_t child = trie[u].child[b];
        if (child < 0) {
          go[u][b] = u == 0 ? 0 : go[fail[u]][b];
          continue;
        }
        if (closesWithin[u]) return false;
        const auto v = static_cast<std::uint32_t>(child);
        fail[v] = u == 0 ? 0 : go[fail[u]][b];
        closesWithin[v] = trie[fail[v]].back >= 0 || closesWithin[fail[v]];
        go[u][b] = v;
        order.push_back(v);
      }
    }
    return true;
  };
  // A closer that is not read, or the end of input, says where the token
  // ends: nothing can follow it in the same token. Elsewhere, and where the
  // trie cannot read the closers, the run is `open + many(!close + any) + close`.
  if (!literal || (!plain && (_matching || !tail)) || !trie_reads()) {
    const Frag open = co_await compile(_image.children[n.first], false);
    const ImageId close = _image.children[n.first + 1];
    const std::uint32_t loop = state();
    eps(open.out, loop);
    const Frag other = co_await condition(close, true);
    eps(loop, other.in);
    const Frag one = char_class(kAnyChar);
    eps(other.out, one.in);
    eps(one.out, loop);
    const Frag closed = co_await compile(close, tail);
    eps(loop, closed.in);
    co_return Frag{open.in, closed.out};
  }
  for (Node& node : trie) node.state = state();
  const std::uint32_t out = state();
  for (std::size_t u = 0; u < trie.size(); ++u) {
    const Node& node = trie[u];
    if (node.back == 0) eps(node.state, out);
    else if (node.back > 0) accept(node.state, static_cast<std::uint8_t>(node.back), LexTest::none);
    else if (eof) accept(node.state, 0, LexTest::eof);
    // By target, the bytes that lead there. Past a closer only one that began
    // where it began can still be read.
    std::map<std::uint32_t, ByteSet> to;
    for (unsigned b = 0; b < 256; ++b) {
      if (node.back >= 0) {
        if (node.child[b] >= 0)
          to[trie[static_cast<std::size_t>(node.child[b])].state].add(b);
      } else {
        to[trie[go[u][b]].state].add(b);
      }
    }
    for (const auto& [target, on] : to) edge(node.state, on, target);
  }
  const std::uint32_t body = trie[0].state;
  const Frag open = co_await compile(_image.children[n.first], false);
  eps(open.out, body);
  co_return Frag{open.in, out};
}

namespace {

// A token ended while `pending` was awaited of the bytes after its last
// lead: `accept` says how far past its end the automaton stands.
struct Post {
  LexAccept accept;
  std::uint8_t pending = kSettled;
  auto operator<=>(const Post&) const = default;
};

void set_paths(LexDfa& dfa, std::size_t nterms) {
  dfa.matcher.matched.resize(nterms + 1);
  bool backed = false;
  for (const auto& set : dfa.acceptSets)
    for (const auto& a : set) {
      if (a.guard == LexTest::match) dfa.matcher.matched[a.term] = true;
      backed = backed || a.back != 0;
    }
  const auto n = dfa.nstates();
  auto& paths = dfa.paths;
  dfa.reach.assign(n, Bits(nterms));
  paths.bodies.resize(n);
  std::vector<std::vector<std::uint16_t>> into(n);
  std::map<std::vector<std::uint16_t>, std::uint32_t> lists;
  std::vector<std::uint16_t> next;
  for (std::uint32_t s = 0; s < n; ++s) {
    next.clear();
    for (const LexAccept& a : dfa.acceptSets[dfa.acceptSet[s]]) dfa.reach[s].set(a.term);
    for (unsigned c = 0; c < dfa.nclasses; ++c) {
      const auto to = dfa.next[s * dfa.nclasses + c];
      if (!to || (!into[to].empty() && into[to].back() == s)) continue;
      into[to].push_back(static_cast<std::uint16_t>(s));
      next.push_back(to);
      if (to == s) paths.bodies[s] = 1;
    }
    const auto [row, fresh] = lists.try_emplace(next, paths.targets.size());
    if (fresh) {
      paths.targets.push_back(static_cast<std::uint16_t>(next.size()));
      paths.targets.insert(paths.targets.end(), next.begin(), next.end());
    }
    paths.offsets.push_back(row->second);
  }
  // What ends here or beyond, and whether a token body can be reached.
  auto body = paths.bodies;
  std::vector<std::uint32_t> todo;
  std::vector<bool> queued(n, true);
  for (std::uint32_t s = 1; s < n; ++s) todo.push_back(s);
  while (!todo.empty()) {
    const auto s = todo.back();
    todo.pop_back();
    queued[s] = false;
    for (const auto from : into[s]) {
      if (from == s) continue;
      const bool grew = dfa.reach[from].merge(dfa.reach[s]);
      if (!grew && body[from] >= body[s]) continue;
      body[from] |= body[s];
      if (!queued[from]) {
        queued[from] = true;
        todo.push_back(from);
      }
    }
  }
  if (n > 1)
    for (unsigned b = 0; b < 256; ++b) paths.openers[b] = body[dfa.next[dfa.nclasses + dfa.classOf[b]]];
  if (backed) dfa.predecessors = std::move(into);
}

}  // namespace

LexDfa LexAutomatonBuilder::build(std::size_t nterms) const {
  std::vector<std::uint32_t> start{0};
  LexDfa dfa = automaton(start);
  set_paths(dfa, nterms);
  dfa.waits = _waits;
  if (!_waits.empty() || _monitored) dfa.monitors = _monitors;
  if (_entries.empty()) return dfa;
  // The matcher's automaton, read from each of its entries.
  std::vector<std::uint32_t> starts = _entries;
  LexDfa read = automaton(starts);
  LexMatcher& out = dfa.matcher;
  out.classOf = read.classOf;
  out.nclasses = read.nclasses;
  out.next = std::move(read.next);
  for (const std::uint16_t set : read.acceptSet) out.accepts.push_back(read.acceptSets[set]);
  out.exits = _exits;
  out.entries.assign(starts.begin(), starts.end());
  // A call with nothing left to read past it goes on as the terminal it
  // calls: that terminal's ends are its caller's.
  for (MatchExit& e : out.exits) {
    if (e.action != MatchAction::call) continue;
    const std::size_t s = out.entries[e.to];
    const auto row = out.next.begin() + static_cast<std::ptrdiff_t>(s * out.nclasses);
    if (out.accepts[s] == std::vector<LexAccept>{LexAccept{}} &&
        std::all_of(row, row + out.nclasses, [](std::uint16_t to) { return to == 0; }))
      e.action = MatchAction::jump;
  }
  return dfa;
}

LexDfa LexAutomatonBuilder::automaton(std::vector<std::uint32_t>& starts) const {
  LexDfa dfa;
  // The states read from the starts.
  std::vector<bool> reached(_states.size(), false);
  {
    std::vector<std::uint32_t> open(starts);
    while (!open.empty()) {
      const std::uint32_t q = open.back();
      open.pop_back();
      if (reached[q]) continue;
      reached[q] = true;
      for (const Edge& e : _states[q].edges) open.push_back(e.to);
      open.insert(open.end(), _states[q].eps.begin(), _states[q].eps.end());
      for (const Test& t : _states[q].tests) open.push_back(t.to);
    }
  }
  // Only paths to an accepting state can contribute a token.
  std::vector<bool> live(_states.size());
  {
    std::vector<std::vector<std::uint32_t>> into(_states.size());
    for (std::uint32_t q = 0; q < _states.size(); ++q) {
      if (!reached[q]) continue;
      for (const Edge& edge : _states[q].edges) into[edge.to].push_back(q);
      for (const auto to : _states[q].eps) into[to].push_back(q);
    }
    std::vector<std::uint32_t> open;
    for (const auto& end : _accepts)
      if (reached[end.first] && !live[end.first]) {
        live[end.first] = true;
        open.push_back(end.first);
      }
    while (!open.empty()) {
      const auto q = open.back();
      open.pop_back();
      for (const auto from : into[q])
        if (!live[from]) {
          live[from] = true;
          open.push_back(from);
        }
    }
  }
  // Bytes no edge tells apart are one class; where a lead is read as one
  // that starts no scalar, neither are the bytes that follow leads alike.
  std::vector<ByteSet> edgeSets;
  bool claims = false;
  for (std::size_t q = 0; q < _states.size(); ++q)
    if (reached[q])
      for (const Edge& e : _states[q].edges) {
        edgeSets.push_back(e.on);
        claims = claims || e.claim != 0;
      }
  if (claims) {
    edgeSets.push_back(ByteSet::range(0x80, 0x8F));
    edgeSets.push_back(ByteSet::range(0x90, 0x9F));
    edgeSets.push_back(ByteSet::range(0xA0, 0xBF));
  }
  std::ranges::sort(edgeSets);
  edgeSets.erase(std::unique(edgeSets.begin(), edgeSets.end()), edgeSets.end());
  {
    std::map<std::vector<bool>, std::uint8_t> classes;
    for (unsigned b = 0; b < 256; ++b) {
      std::vector<bool> key(edgeSets.size());
      for (std::size_t i = 0; i < edgeSets.size(); ++i) key[i] = edgeSets[i].has(b);
      dfa.classOf[b] = classes
                           .try_emplace(std::move(key),
                                        static_cast<std::uint8_t>(classes.size()))
                           .first->second;
    }
    dfa.nclasses = static_cast<unsigned>(classes.size());
  }
  // Per distinct edge set, the classes it holds; per edge, in the order the
  // states hold them, its set; per class, how its bytes follow a lead.
  std::vector<std::vector<unsigned>> held(edgeSets.size());
  std::vector<Follower> followerOf(dfa.nclasses, Follower::other);
  {
    std::array<unsigned, 256> sample{};
    for (unsigned b = 256; b-- > 0;) sample[dfa.classOf[b]] = b;
    for (std::size_t i = 0; i < edgeSets.size(); ++i)
      for (unsigned c = 0; c < dfa.nclasses; ++c)
        if (edgeSets[i].has(sample[c])) held[i].push_back(c);
    if (claims)
      for (unsigned c = 0; c < dfa.nclasses; ++c) followerOf[c] = follower(sample[c]);
  }
  std::vector<std::uint32_t> edgesAt(_states.size(), 0);
  std::vector<std::uint32_t> setOf;
  for (std::size_t s = 0; s < _states.size(); ++s) {
    if (!reached[s]) continue;
    edgesAt[s] = static_cast<std::uint32_t>(setOf.size());
    for (const Edge& e : _states[s].edges)
      setOf.push_back(static_cast<std::uint32_t>(
          std::ranges::lower_bound(edgeSets, e.on) - edgeSets.begin()));
  }

  std::vector<std::vector<LexAccept>> acceptsAt(_states.size());
  for (const auto& [at, what] : _accepts) acceptsAt[at].push_back(what);

  // The automaton walks threads: a state with what is awaited of the bytes
  // after a lead it read as starting no scalar. A token that ends while that
  // is awaited goes on as a thread of its own, past its end, until the bytes
  // settle it: `posts`, numbered after the states' threads.
  const auto postBase = static_cast<std::uint32_t>(_states.size() * kPendingCount);
  std::vector<Post> posts;
  std::map<Post, std::uint32_t> postIds;
  // A stamp visits each thread once per closure.
  using Set = std::vector<std::uint32_t>;
  std::vector<std::uint32_t> seen(postBase, 0);
  std::uint32_t stamp = 0;
  std::vector<std::uint32_t> work;
  // The thread of `accept` one byte further past its end.
  const auto post = [&](LexAccept accept, std::uint8_t pending) {
    if (accept.back == 0xFF) throw Refused("a token is settled too far past its end");
    ++accept.back;
    const auto [it, fresh] = postIds.try_emplace(
        Post{accept, pending}, postBase + static_cast<std::uint32_t>(posts.size()));
    if (fresh) {
      posts.push_back(it->first);
      seen.push_back(0);
    }
    return it->second;
  };
  const auto close = [&](Set& set) {
    if (++stamp == 0) {
      std::ranges::fill(seen, 0);
      ++stamp;
    }
    work.swap(set);
    set.clear();
    while (!work.empty()) {
      const auto thread = work.back();
      work.pop_back();
      if (seen[thread] == stamp) continue;
      seen[thread] = stamp;
      if (thread >= postBase) {
        set.push_back(thread);
        continue;
      }
      const auto q = thread / kPendingCount;
      if (q == starts.front() || (live[q] && (!_states[q].edges.empty() || !acceptsAt[q].empty())))
        set.push_back(thread);
      if (live[q])
        for (const auto to : _states[q].eps)
          if (live[to]) work.push_back(to * kPendingCount + thread % kPendingCount);
    }
    std::ranges::sort(set);
  };

  // The sets met, each kept once as the key that numbers it, and the set of
  // terminals that end in each. The empty set is the dead state.
  struct SetHash {
    std::size_t operator()(const Set& set) const noexcept {
      std::uint64_t hash = 14695981039346656037ull;
      for (const auto thread : set) hash = (hash ^ thread) * 1099511628211ull;
      return static_cast<std::size_t>(hash);
    }
  };
  std::unordered_map<Set, std::uint32_t, SetHash> ids;
  std::vector<const Set*> sets{nullptr};
  std::vector<std::uint32_t> acceptOf{0};
  std::map<std::vector<LexAccept>, std::uint32_t> acceptIds{{{}, 0}};
  dfa.acceptSets.assign(1, {});
  const auto intern = [&](const Set& set) -> std::uint32_t {
    if (set.empty()) return 0;
    const auto [it, fresh] = ids.try_emplace(set, static_cast<std::uint32_t>(sets.size()));
    if (!fresh) return it->second;
    // Where the last lead is not settled yet, a token ends only with the
    // input; a matcher candidate ends where the matcher reads it to.
    std::vector<LexAccept> accepts;
    const auto ends = [&](LexAccept accept, std::uint8_t pending) {
      if (pending != kSettled && accept.guard != LexTest::match) {
        if (accept.guard == LexTest::word) return;
        accept.guard = LexTest::eof;
      }
      accepts.push_back(accept);
    };
    for (const std::uint32_t thread : set) {
      if (thread >= postBase) {
        ends(posts[thread - postBase].accept, posts[thread - postBase].pending);
        continue;
      }
      for (const LexAccept& accept : acceptsAt[thread / kPendingCount])
        ends(accept, static_cast<std::uint8_t>(thread % kPendingCount));
    }
    std::ranges::sort(accepts);
    accepts.erase(std::unique(accepts.begin(), accepts.end()), accepts.end());
    const auto [at, added] = acceptIds.try_emplace(
        std::move(accepts), static_cast<std::uint32_t>(dfa.acceptSets.size()));
    if (added) dfa.acceptSets.push_back(at->first);
    sets.push_back(&it->first);
    acceptOf.push_back(at->second);
    return it->second;
  };
  std::vector<std::uint32_t> begun;
  for (const std::uint32_t q : starts) {
    Set start{static_cast<std::uint32_t>(q * kPendingCount)};
    close(start);
    begun.push_back(intern(start));
  }
  std::vector<std::uint32_t> next(dfa.nclasses, 0);
  std::vector<Set> byClass(dfa.nclasses);
  Set target;
  std::unordered_map<Set, std::uint32_t, SetHash> transitions;
  // A token ended while `pending` was awaited: on each class that does not
  // break it, its thread one byte further.
  const auto await = [&](const LexAccept& accept, std::uint8_t pending) {
    for (unsigned c = 0; c < dfa.nclasses; ++c)
      if (const std::uint8_t then = after(pending, followerOf[c]); then != kBroken)
        byClass[c].push_back(post(accept, then));
  };
  for (std::size_t at = 1; at < sets.size(); ++at) {
    next.resize((at + 1) * dfa.nclasses, 0);
    for (Set& to : byClass) to.clear();
    for (const std::uint32_t thread : *sets[at]) {
      if (thread >= postBase) {
        // Copied: awaiting may make more posts.
        if (const Post waiting = posts[thread - postBase]; waiting.pending != kSettled)
          await(waiting.accept, waiting.pending);
        continue;
      }
      const std::uint32_t s = thread / kPendingCount;
      const auto pending = static_cast<std::uint8_t>(thread % kPendingCount);
      const std::vector<Edge>& edges = _states[s].edges;
      for (std::size_t k = 0; k < edges.size(); ++k) {
        if (!live[edges[k].to]) continue;
        for (const unsigned c : held[setOf[edgesAt[s] + k]]) {
          const std::uint8_t then =
              edges[k].claim != 0 ? edges[k].claim : after(pending, followerOf[c]);
          if (then != kBroken) byClass[c].push_back(edges[k].to * kPendingCount + then);
        }
      }
      if (pending == kSettled) continue;
      for (const LexAccept& accept : acceptsAt[s])
        if (accept.guard == LexTest::none) await(accept, pending);
    }
    // Classes with identical moves share the closure of their first class.
    auto row = std::span(next).last(dfa.nclasses);
    for (unsigned c = 0; c < dfa.nclasses;) {
      const Set& moves = byClass[c];
      unsigned end = c + 1;
      while (end < dfa.nclasses && byClass[end] == moves) ++end;
      if (!moves.empty()) {
        auto [to, fresh] = transitions.try_emplace(moves, 0);
        if (fresh) {
          target = moves;
          close(target);
          to->second = intern(target);
        }
        std::fill(row.begin() + c, row.begin() + end, to->second);
      }
      c = end;
    }
  }
  const std::size_t n = sets.size();
  ids.clear();
  transitions.clear();
  next.resize(n * dfa.nclasses, 0);

  // A state no terminal can still end from reads nothing: it is the dead one.
  {
    std::vector<std::vector<std::uint32_t>> into(n);
    for (std::size_t s = 1; s < n; ++s)
      for (unsigned c = 0; c < dfa.nclasses; ++c)
        if (const auto to = next[s * dfa.nclasses + c];
            to != 0 && to != s && (into[to].empty() || into[to].back() != s))
          into[to].push_back(static_cast<std::uint32_t>(s));
    std::vector<bool> live(n, false);
    std::vector<std::uint32_t> reach;
    for (std::size_t s = 1; s < n; ++s)
      if (acceptOf[s] != 0) {
        live[s] = true;
        reach.push_back(static_cast<std::uint32_t>(s));
      }
    while (!reach.empty()) {
      const std::uint32_t s = reach.back();
      reach.pop_back();
      for (const std::uint32_t from : into[s])
        if (!live[from]) {
          live[from] = true;
          reach.push_back(from);
        }
    }
    for (std::uint32_t& to : next)
      if (!live[to]) to = 0;
  }

  // States no input tells apart are one: those where the same terminals end
  // and that lead, class by class, to states that are one.
  auto part = acceptOf;
  refine_partition(part, static_cast<std::uint32_t>(dfa.acceptSets.size()), dfa.nclasses, [&](auto add) {
    for (std::uint32_t s = 0; s < n; ++s)
      for (unsigned c = 0; c < dfa.nclasses; ++c) add(s, next[std::size_t{s} * dfa.nclasses + c], c);
  });
  // The dead state keeps number 0 and the start number 1, a state of its own
  // even where it reads nothing; the others follow in the order first met.
  std::vector<std::uint32_t> number(n, 0);
  std::vector<std::size_t> representative{0, 1};
  constexpr std::uint32_t kUnnumbered = ~std::uint32_t{0};
  std::vector<std::uint32_t> numbered(n, kUnnumbered);
  numbered[part[0]] = 0;
  number[1] = 1;
  if (numbered[part[1]] == kUnnumbered) numbered[part[1]] = 1;
  for (std::size_t s = 2; s < n; ++s) {
    std::uint32_t& at = numbered[part[s]];
    if (at == kUnnumbered) {
      at = static_cast<std::uint32_t>(representative.size());
      representative.push_back(s);
    }
    number[s] = at;
  }
  const std::size_t kept = representative.size();
  if (kept >= 0xFFFF) throw Refused("the terminals need more than 65534 lexer states");
  dfa.next.assign(kept * dfa.nclasses, 0);
  dfa.acceptSet.assign(kept, 0);
  for (std::size_t k = 1; k < kept; ++k) {
    const std::size_t s = representative[k];
    for (unsigned c = 0; c < dfa.nclasses; ++c)
      dfa.next[k * dfa.nclasses + c] = static_cast<std::uint16_t>(number[next[s * dfa.nclasses + c]]);
    dfa.acceptSet[k] = static_cast<std::uint16_t>(acceptOf[s]);
  }

  for (std::size_t k = 0; k < starts.size(); ++k) starts[k] = number[begun[k]];
  return dfa;
}

}  // namespace cursiv::engine::gen
