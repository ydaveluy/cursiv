// The matcher's search: threads of its automaton, each an entry at a place
// with the marks it holds, inside a call, with the calls and the conditions
// they meet.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Machine.hpp"

namespace cursiv::engine {

// A mark's bytes: not begun, begun and not ended, or both.
inline constexpr TextOffset kNoMark = ~TextOffset{0};
struct MatchMark {
  TextOffset begin = kNoMark;
  TextOffset end = kNoMark;
  friend bool operator==(const MatchMark&, const MatchMark&) = default;
};

struct MatchThread {
  std::uint32_t entry = 0;
  TextOffset at = 0;
  std::uint32_t call = 0;
  std::array<MatchMark, kMatchTags> marks{};
  friend bool operator==(const MatchThread&, const MatchThread&) = default;
};

// One search: from one thread, the furthest end of call 0, which no thread
// calls; with `any`, its first end is enough. A thread is read once past the
// first few. A call is of an entry at a place: its callers go on from each
// place it ends, whenever they came. A condition a thread meets waits in
// `asked` for a search of its own.
class Matching {
 public:
  struct Place {
    std::uint32_t entry = 0;
    TextOffset at = 0;
    friend bool operator==(const Place&, const Place&) = default;
  };
  struct Asked {
    std::uint32_t entry = 0;
    bool unless = false;
    MatchThread then;
  };

  void start(Machine& machine, const MatchThread& first, bool any) {
    _machine = &machine;
    _endReads = machine.endReads;
    _any = any;
    _furthest = kNoMark;
    _made = 0;
    _jumped = false;
    _todo.clear();
    _seen.clear();
    _calls.resize(1);
    _calls.front().called = {first.entry, first.at};
    _callOf.clear();
    _callers.clear();
    _pending.clear();
    _passed.clear();
    asked.clear();
    go(first);
  }
  // Whether a thread waits, no condition waits for its answer, and more may
  // be found.
  [[nodiscard]] bool runs() const { return !_todo.empty() && asked.empty() && !(_any && found()); }
  [[nodiscard]] bool done() const { return (_any && found()) || (_todo.empty() && asked.empty()); }
  [[nodiscard]] bool found() const { return _furthest != kNoMark; }
  [[nodiscard]] TextOffset furthest() const { return _furthest; }

  MatchThread take() {
    watch();
    const MatchThread th = _todo.back();
    _todo.pop_back();
    return th;
  }
  void go(const MatchThread& next) {
    if (_made < kUnchecked) {
      ++_made;
    } else {
      if (!_seen.try_emplace(next, true).second) return;
      if (next.call == 0 && next.marks == decltype(next.marks){}) {
        const auto key = stretch_place(kUnmatched, next.entry, next.at);
        if (_machine->unmatched(key)) return;
        _passed.push_back(key);
      }
    }
    _todo.push_back(next);
  }
  // A scanner can end beyond a DFA checkpoint that cannot reach that end itself.
  void end(std::uint32_t call, TextOffset at, bool jumped = false) {
    if (jumped) _jumped = true;
    if (call != 0) return ended(call, at, at);
    if (_furthest == kNoMark || at > _furthest) _furthest = at;
  }
  // Whether the text at `at` reads mark `mark` again, as it was written.
  [[nodiscard]] bool echoes(Machine& m, MatchMark mark, TextOffset at);
  void ask(std::uint32_t entry, bool unless, const MatchThread& then) {
    asked.push_back({entry, unless, then});
  }
  // `then` calls `callee` where it stands, and goes on past each end, and
  // past one byte of `suffix` after it where given.
  void call(Machine& m, std::uint32_t callee, const MatchThread& then,
            const std::uint64_t* suffix = nullptr);
  bool propagate(Machine& m);
  // Takes the steps of its threads until none runs.
  void run(Machine& m);
  // The furthest end of `entry` at `begin`, null for none, where one thread
  // reads it within its first stride and only ends: false where a search
  // must read it.
  [[nodiscard]] static bool alone(Machine& m, std::uint32_t entry, const char* begin,
                                  const char*& found);
  // The place past `p` where a thread stops to be watched and weighed: a
  // stride of the text on.
  [[nodiscard]] static const char* stride(const Machine& m, const char* p) {
    return p + std::min(m.mark - p, kQuietSpan - ((p - m.src) & (kQuietSpan - 1)));
  }
  // At a stride, reuse a known suffix for a markless thread. Called rules
  // need every end. The root needs only the furthest one.
  bool passes(Machine& m, const MatchThread& th, std::uint32_t state, const char* p,
              const char*& limit) {
    if (p >= m.mark) m.watch(p);
    limit = stride(m, p);
    if (th.marks != decltype(th.marks){}) return true;
    const auto at = static_cast<TextOffset>(p - m.src);
    const auto key = stretch_place(kMatchState, state, at);
    const auto it = m.stretches.find(key);
    if (it != m.stretches.end() && (it->second.end == at || th.call == 0)) {
      m.observe_end(it->second.atCut);
      if (it->second.end != at) end(0, it->second.end);
      return false;
    }
    if (th.call == 0) _passed.push_back(key);
    return true;
  }
  // A condition's search leaves the ends of its calls to the token's later
  // searches. A failed search proves its visited suffixes empty and its calls
  // read every way; on a single thread with no scanner's end, an end beyond a
  // visited place belongs to that suffix.
  void remember(Machine& m);

  std::vector<Asked> asked;

 private:
  static constexpr std::uint32_t kNoLink = ~std::uint32_t{0};
  // The threads a search reads as they come, and their runs whole, before it
  // reads each once and their runs through the known runs.
  static constexpr std::uint32_t kUnchecked = 32;
  using Ranges = std::map<TextOffset, TextOffset>;
  struct Call {
    Place called;
    std::uint32_t callers = kNoLink;
    Ranges ends{};
  };
  struct Caller {
    MatchThread then;
    const std::uint64_t* suffix;
    std::uint32_t next;
    Ranges pending{};
  };
  void watch() {
    if ((++_steps & 0x3FF) == 0) throwIfCancelled(_machine->stop);
  }
  template <class Each>
  void add(Ranges& ranges, TextOffset first, TextOffset last, Each each);
  // Whether the test of end `x` holds at `p`.
  [[nodiscard]] static bool holds(Machine& m, const MatchEnd& x, const char* p);
  // The bytes the scanner of exit `e` reads from `at`: none where it refuses.
  [[nodiscard]] static std::size_t scanned(Machine& m, const MatchTables& a, const MatchExit& e,
                                           const char* at);
  // Thread `th` takes exit `e` at `at`.
  void leave(Machine& m, const MatchTables& a, const MatchThread& th, const MatchExit& e,
             TextOffset at);
  void ended(std::uint32_t call, TextOffset first, TextOffset last);
  void resume(std::uint32_t caller, TextOffset first, TextOffset last);

  // Keys found again by their hash, each with a value: emptied at once, the
  // table keeps its room for the next search.
  template <class Key, class Value>
  class Table {
   public:
    void clear() {
      _size = 0;
      if (++_stamp != 0) return;
      for (Slot& slot : _slots) slot.stamp = 0;
      _stamp = 1;
    }
    // The value of `key`, `made` where it had none; and whether it was made.
    std::pair<Value&, bool> try_emplace(const Key& key, Value made) {
      if (_slots.empty()) _slots.resize(16);
      Slot* slot = &place(key);
      if (slot->stamp == _stamp) return {slot->value, false};
      if (2 * _size == _slots.size()) {
        grow();
        slot = &place(key);
      }
      *slot = {key, made, _stamp};
      ++_size;
      return {slot->value, true};
    }

   private:
    static_assert(std::has_unique_object_representations_v<Key> && sizeof(Key) % 4 == 0);
    struct Slot {
      Key key{};
      Value value{};
      std::uint32_t stamp = 0;
    };
    static std::size_t hash(const Key& key) {
      std::array<std::uint32_t, sizeof(Key) / 4> words;
      std::memcpy(words.data(), &key, sizeof(Key));
      std::uint64_t h = 0;
      for (const std::uint32_t word : words) {
        h = (h ^ word) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
      }
      return static_cast<std::size_t>((h * 0xC4CEB9FE1A85EC53ull) >> 32);
    }
    Slot& place(const Key& key) {
      for (std::size_t i = hash(key);; ++i) {
        Slot& slot = _slots[i & (_slots.size() - 1)];
        if (slot.stamp != _stamp || slot.key == key) return slot;
      }
    }
    void grow() {
      const auto held = std::exchange(_slots, std::vector<Slot>(2 * _slots.size()));
      for (const Slot& slot : held)
        if (slot.stamp == _stamp) place(slot.key) = slot;
    }
    std::vector<Slot> _slots;
    std::uint32_t _stamp = 1;
    std::size_t _size = 0;
  };


  Machine* _machine = nullptr;
  std::uint64_t _endReads = 0;
  bool _any = false;
  TextOffset _furthest = kNoMark;
  // The threads made, counted up to the first unchecked one.
  std::uint32_t _made = 0;
  // Whether a scanner's end was found.
  bool _jumped = false;
  std::uint32_t _steps = 0;
  std::vector<MatchThread> _todo;
  Table<MatchThread, bool> _seen;
  std::vector<Call> _calls;
  Table<Place, std::uint32_t> _callOf;
  std::vector<Caller> _callers;
  std::vector<std::uint32_t> _pending;
  // Markless root entries and DFA checkpoints visited during this search.
  std::vector<std::uint64_t> _passed;
};

}  // namespace cursiv::engine
