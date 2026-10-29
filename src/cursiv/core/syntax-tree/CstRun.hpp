#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <limits>
#include <stdexcept>
#include <utility>

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/macros.hpp"

// The postorder run a tree is made of: one `CstNode` per record, in the
// order they close, record index is the node id -- what an engine writes is
// what every reader reads.

namespace cursiv {

// The records, and how many. The buffer is not a vector: a vector's only way
// to hand out room is `resize`, which value-initialises what the writer is
// about to overwrite.
class CstRun {
 public:
  CstRun() = default;
  // A move carries the records, never a watch: the stop belongs to the
  // parse writing the source.
  CstRun(CstRun&& other) noexcept
      : _r(std::exchange(other._r, nullptr)),
        _cap(std::exchange(other._cap, 0)),
        _mark(_cap),
        _records(std::exchange(other._records, 0)),
        _reserveHint(std::exchange(other._reserveHint, 0)),
        _mem(std::exchange(other._mem, nullptr)) {
    other._mark = 0;
    other._stop = nullptr;
  }
  CstRun& operator=(CstRun&& other) noexcept {
    if (this == &other) return *this;
    if (_mem == nullptr) delete[] _r;
    _r = std::exchange(other._r, nullptr);
    _cap = std::exchange(other._cap, 0);
    _mark = _cap;
    _stop = nullptr;
    _records = std::exchange(other._records, 0);
    _reserveHint = std::exchange(other._reserveHint, 0);
    _mem = std::exchange(other._mem, nullptr);
    other._mark = 0;
    other._stop = nullptr;
    return *this;
  }
  ~CstRun() {
    if (_mem == nullptr) delete[] _r;
  }

  // The tree's own arena, and never a second allocator: the records and the
  // AST both come from it, so a parse frees in one release and the AST build
  // reads records laid next to the nodes it writes. A doubling leaves its
  // predecessor behind -- a monotonic resource has no free -- which is what
  // makes sizing from the document worth doing. Binding drops the buffer;
  // unbound, the run owns a heap buffer.
  void bind(std::pmr::memory_resource* mem) {
    if (_mem == nullptr) delete[] _r;
    _r = nullptr;
    _cap = 0;
    _mark = 0;
    _stop = nullptr;
    _records = 0;
    _reserveHint = 0;
    _mem = mem;
  }

  // Sized from the document at a record per byte: room reserved past the
  // need is never touched, so natively it never becomes resident. Wasm linear
  // memory charges untouched room too: there a quarter is estimated, and a
  // large one is taken only once records reach it, in one step, so a dense
  // tree strands no series of smaller buffers in the monotonic pool.
  void reserve_for(std::size_t textBytes) {
    const std::size_t words = sizeof(void*) < 8 ? textBytes / 4 : textBytes;
    std::size_t want =
        words > kMaxRecords - 64 ? kMaxRecords : words + 64;
    if constexpr (sizeof(void*) < 8) {
      _reserveHint = static_cast<std::uint32_t>(want);
      if (words > kFirstRecords * 4) want = kFirstRecords;
    }
    if (want > _cap) reallocate(want);
  }

  // The records past zero are room again: the buffer stays.
  void clear() { _records = 0; }

  // A parse's stop, read once per run of records by the write that reaches
  // the watermark: the test that guards the room is the only per-record
  // cost. Null, or a token that cannot stop, watches nothing.
  void watch(const CancellationToken* stop) {
    _stop = stop != nullptr && stop->stop_possible() ? stop : nullptr;
    rearm();
  }

  // `watch` for the scope of the parse that writes the run.
  class Watch {
   public:
    Watch(CstRun& run, const CancellationToken& stop) : _run(run) {
      _run.watch(&stop);
    }
    ~Watch() { _run.watch(nullptr); }
    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;

   private:
    CstRun& _run;
  };

  [[nodiscard]] NodeCount records() const { return _records; }
  [[nodiscard]] bool empty() const { return _records == 0; }
  [[nodiscard]] const CstNode* data() const { return _r; }
  [[nodiscard]] std::size_t capacity() const { return _cap; }

  CURSIV_ALWAYS_INLINE void leaf(TextOffset begin, TextOffset end,
                                 std::uint32_t engine) {
    put(begin, end, 1, engine, false, false);
  }
  CURSIV_ALWAYS_INLINE void hidden(TextOffset begin, TextOffset end,
                                   std::uint32_t engine) {
    put(begin, end, 1, engine, true, false);
  }
  CURSIV_ALWAYS_INLINE void repaired_leaf(TextOffset begin, TextOffset end,
                                          std::uint32_t engine) {
    put(begin, end, 1, engine, false, true);
  }
  CURSIV_ALWAYS_INLINE void close(TextOffset begin, TextOffset end,
                                  NodeCount extent, std::uint32_t engine) {
    put(begin, end, extent, engine, false, false);
  }
  void repaired_close(TextOffset begin, TextOffset end, NodeCount extent,
                      std::uint32_t engine) {
    put(begin, end, extent, engine, false, true);
  }

  // The run is raw storage and a word index is arithmetic -- a base
  // subtracted from a coordinate the caller carried across a fork. Asserted,
  // so a coordinate that outlived its run says so instead of reading a
  // record that is not there.
  [[nodiscard]] const CstNode& at(std::size_t i) const {
    assert(i < _records && "a word index stands inside its own run");
    return _r[i];
  }
  [[nodiscard]] CstNode& at(std::size_t i) {
    assert(i < _records && "a word index stands inside its own run");
    return _r[i];
  }

  // One record opened in the middle: a parent that must precede the trailing
  // trivia already on the run. Off the per-record path -- the suffix it
  // moves is that trivia, never the tree.
  CURSIV_NOINLINE void insert(NodeId at, TextOffset begin, TextOffset end,
                              NodeCount extent, std::uint32_t engine) {
    assert(at <= _records && "an insertion stands inside its own run");
    reserve(1);
    for (std::size_t i = _records; i > at; --i) _r[i] = _r[i - 1];
    ++_records;
    fill(_r[at], begin, end, extent, engine, false, false);
  }

 private:
  // Field by field: the bit-field byte is written once rather than read back
  // and merged.
  CURSIV_ALWAYS_INLINE static void fill(CstNode& n, TextOffset begin,
                                        TextOffset end, NodeCount extent,
                                        std::uint32_t engine, bool hidden,
                                        bool recovered) {
    n.begin = begin;
    n.end = end;
    n.subtreeSize = extent;
    n.hidden = hidden;
    n.recovered = recovered;
    n.engine = engine;
  }

  CURSIV_ALWAYS_INLINE void put(TextOffset begin, TextOffset end,
                                NodeCount extent, std::uint32_t engine,
                                bool hidden, bool recovered) {
    reserve(1);
    fill(_r[_records++], begin, end, extent, engine, hidden, recovered);
  }

  CURSIV_ALWAYS_INLINE void reserve(std::size_t k) {
    if (_records + k > _mark) [[unlikely]]
      make_room(_records + k);
  }
  // Where the watermark stops a write: a watched stop is read here, and the
  // room grows only when the write reaches its end.
  CURSIV_NOINLINE void make_room(std::size_t need) {
    if (_stop != nullptr) throw_if_cancelled(*_stop);
    if (need > _cap) grow(need);
    rearm();
  }
  // The room's end, or sooner where a stop is watched.
  void rearm() {
    _mark = _stop != nullptr && _cap - _records > kWatchedRecords
                ? _records + kWatchedRecords
                : _cap;
  }
  // Off the per-record path, and the one id-space check: every writer grows
  // through here, doubling but never past what was asked.
  CURSIV_NOINLINE void grow(std::size_t need) {
    if (need > kMaxRecords) [[unlikely]]
      throw std::overflow_error("cursiv: CST exceeds the node-id space");
    // Only the arena jumps to 1 024, since it keeps every step it leaves.
    const std::size_t twice =
        _cap == 0 ? 64 : _cap < 1024 && _mem != nullptr ? 1024 : _cap * 2;
    const std::size_t want = sizeof(void*) < 8 && _reserveHint > _cap
                                 ? _reserveHint
                                 : twice;
    reallocate(want < need ? need : want < kMaxRecords ? want : kMaxRecords);
  }
  // It never zeroes: only the records already written are carried over, the
  // rest is room.
  CURSIV_NOINLINE void reallocate(std::size_t cap) {
    CstNode* const q = _mem != nullptr
                           ? static_cast<CstNode*>(_mem->allocate(
                                 cap * sizeof(CstNode), alignof(CstNode)))
                           : new CstNode[cap];
    for (std::size_t i = 0; i < _records; ++i) q[i] = _r[i];
    if (_mem == nullptr) delete[] _r;
    _r = q;
    _cap = cap;
    rearm();
  }

  // `kNoNode` is no record, and a count is stated as an id too.
  static constexpr std::size_t kByBytes =
      std::numeric_limits<std::size_t>::max() / sizeof(CstNode);
  static constexpr std::size_t kMaxRecords =
      kByBytes < kNoNode - 1 ? kByBytes : kNoNode - 1;
  static constexpr std::size_t kFirstRecords = 64 * 1024;
  // About a millisecond of parsing between two reads of a watched stop.
  static constexpr std::size_t kWatchedRecords = 64 * 1024;

  CstNode* _r = nullptr;
  std::size_t _cap = 0;
  // Where a write leaves the per-record path: `_cap`, or the next read of
  // the watched stop.
  std::size_t _mark = 0;
  const CancellationToken* _stop = nullptr;
  NodeCount _records = 0;
  std::uint32_t _reserveHint = 0;
  std::pmr::memory_resource* _mem = nullptr;
};

}  // namespace cursiv
