#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <limits>
#include <stdexcept>
#include <utility>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"

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
  CstRun(CstRun&& other) noexcept { *this = std::move(other); }
  CstRun& operator=(CstRun&& other) noexcept {
    if (this == &other) return *this;
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

  // The tree's arena owns the records and the AST. Binding forgets the
  // previous buffer.
  void bind(std::pmr::memory_resource* mem) {
    _r = nullptr;
    _cap = 0;
    _mark = 0;
    _stop = nullptr;
    _records = 0;
    _reserveHint = 0;
    _mem = mem;
  }

  // Room for `records` now; the room past the records is never written.
  // Where `later`, the records a text of `later` bytes takes at most, is
  // more, a growth goes in one step to what the whole text takes at the
  // density read so far.
  void reserveRecords(std::size_t records, std::size_t later = 0) {
    _reserveHint =
        static_cast<std::uint32_t>(later < kMaxRecords ? later : kMaxRecords);
    if (records > kMaxRecords) records = kMaxRecords;
    if (records > _cap) reallocate(records);
  }

  // The records from `records` on are room again: the buffer stays.
  void truncate(NodeCount records) {
    assert(records <= _records);
    _records = records;
  }
  void clear() { truncate(0); }

  // A parse's cancellation token, checked by the write that reaches `_mark`,
  // so the room check stays the only per-record cost. Null, or a token that
  // cannot stop, disables it.
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

  // Whether the next records fit before the next capacity or cancellation check.
  [[nodiscard]] bool room(std::size_t n = 1) const {
    return _records <= _mark && n <= _mark - _records;
  }
  // Room for `need` records in all, a watched stop read first.
  CURSIV_NOINLINE void makeRoom(std::size_t need) {
    if (_stop != nullptr) throwIfCancelled(*_stop);
    if (need > _cap) grow(need);
    rearm();
  }

  // With `Roomed`, the writer made the room first: `room`.
  template <bool Roomed = false>
  CURSIV_ALWAYS_INLINE void leaf(TextOffset begin, TextOffset end,
                                 std::uint32_t element) {
    put<Roomed>(begin, end, 1, element, false, false);
  }
  CURSIV_ALWAYS_INLINE void hidden(TextOffset begin, TextOffset end,
                                   std::uint32_t element) {
    put(begin, end, 1, element, true, false);
  }
  CURSIV_ALWAYS_INLINE void repairedLeaf(TextOffset begin, TextOffset end,
                                          std::uint32_t element) {
    put(begin, end, 1, element, false, true);
  }
  template <bool Roomed = false>
  CURSIV_ALWAYS_INLINE void close(TextOffset begin, TextOffset end,
                                  NodeCount extent, std::uint32_t element) {
    put<Roomed>(begin, end, extent, element, false, false);
  }

  // A word index is a base subtracted from a coordinate, so it is asserted to
  // stand inside the run.
  [[nodiscard]] const CstNode& at(std::size_t i) const {
    assert(i < _records && "a word index stands inside its own run");
    return _r[i];
  }
  [[nodiscard]] CstNode& at(std::size_t i) {
    assert(i < _records && "a word index stands inside its own run");
    return _r[i];
  }

 private:
  CURSIV_ALWAYS_INLINE static void fill(CstNode& n, TextOffset begin,
                                        TextOffset end, NodeCount extent,
                                        std::uint32_t element, bool hidden,
                                        bool recovered) {
    n.begin = begin;
    n.end = end;
    n.subtreeSize = extent;
    n.setEngine(element);
    n.setHidden(hidden);
    n.setRecovered(recovered);
  }

  template <bool Roomed = false>
  CURSIV_ALWAYS_INLINE void put(TextOffset begin, TextOffset end,
                                NodeCount extent, std::uint32_t element,
                                bool hidden, bool recovered) {
    if (!Roomed && !room()) [[unlikely]]
      makeRoom(_records + 1);
    assert(room());
    fill(_r[_records++], begin, end, extent, element, hidden, recovered);
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
        _cap == 0 ? 64 : _cap < 1024 ? 1024 : _cap * 2;
    std::size_t want = twice;
    if (const TextOffset read = _records == 0 ? 0 : _r[_records - 1].end;
        _reserveHint > _cap && read != 0) {
      // Forecast from the observed density. On 32-bit targets grow past 16 MiB in steps.
      const double whole = static_cast<double>(_records) * 1.125 * _reserveHint / read;
      want = static_cast<std::size_t>(std::min<double>(_reserveHint, std::max<double>(twice, whole)));
      if constexpr (sizeof(std::size_t) == 4)
        want = std::min(want, std::max(twice, (std::size_t{16} << 20) / sizeof(CstNode)));
    }
    reallocate(want < need ? need : want < kMaxRecords ? want : kMaxRecords);
  }
  // It never zeroes: only the records already written are carried over, the
  // rest is room.
  CURSIV_NOINLINE void reallocate(std::size_t cap) {
    auto* const q = static_cast<CstNode*>(
        _mem->allocate(cap * sizeof(CstNode), alignof(CstNode)));
    if (_records != 0) std::memcpy(q, _r, _records * sizeof(CstNode));
    if (_r != nullptr)
      _mem->deallocate(_r, _cap * sizeof(CstNode), alignof(CstNode));
    _r = q;
    _cap = cap;
    rearm();
  }

  // `kNoNode` is no record, and a count is stated as an id too.
  static constexpr std::size_t kByBytes =
      std::numeric_limits<std::size_t>::max() / sizeof(CstNode);
  static constexpr std::size_t kMaxRecords =
      kByBytes < kNoNode - 1 ? kByBytes : kNoNode - 1;
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
