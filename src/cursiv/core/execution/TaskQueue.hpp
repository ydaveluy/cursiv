#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Taken from Taskflow (taskflow/core/tsq.hpp), MIT licence:
//   Copyright (c) 2018-2024 Dr. Tsung-Wei Huang, University of Wisconsin.
// The algorithm is unchanged; only the spelling follows this tree.

// Chase-Lev work-stealing deque: the owner pushes and pops at the bottom
// lock-free; a thief steals from the top, and a compare-exchange arbitrates
// the last item. Old arrays are kept, not freed, since a thief may still be
// reading one after a resize; only the owner's destruction is safe to reclaim.

namespace cursiv {

template <class T>
class TaskQueue {
  static_assert(sizeof(T) == sizeof(void*), "T must be a pointer type");

 public:
  explicit TaskQueue(std::int64_t logSize = 10)
      : _array(nullptr) {
    _garbage.reserve(32);
    _array.store(new Array{std::int64_t{1} << logSize},
                  std::memory_order_relaxed);
  }
  ~TaskQueue() {
    for (Array* const array : _garbage) delete array;
    delete _array.load();
  }
  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;

  [[nodiscard]] bool empty() const noexcept {
    return _bottom.load(std::memory_order_relaxed) <=
           _top.load(std::memory_order_relaxed);
  }

  // Owner only.
  void push(T item) {
    const std::int64_t bottom = _bottom.load(std::memory_order_relaxed);
    const std::int64_t top = _top.load(std::memory_order_acquire);
    Array* array = _array.load(std::memory_order_relaxed);

    if (array->capacity - 1 < bottom - top) array = resize(array, bottom, top);

    array->put(bottom, item);
    std::atomic_thread_fence(std::memory_order_release);
    _bottom.store(bottom + 1, std::memory_order_release);
  }

  // Owner only. Null when the queue was empty, or when a thief won the race
  // for the last item.
  [[nodiscard]] T pop() {
    std::int64_t bottom = _bottom.load(std::memory_order_relaxed) - 1;
    Array* const array = _array.load(std::memory_order_relaxed);
    _bottom.store(bottom, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::int64_t top = _top.load(std::memory_order_relaxed);

    T item = nullptr;
    if (top <= bottom) {
      item = array->get(bottom);
      if (top == bottom) {
        // The last item, and a thief may be taking it right now.
        if (!_top.compare_exchange_strong(top, top + 1,
                                          std::memory_order_seq_cst,
                                          std::memory_order_relaxed))
          item = nullptr;
        _bottom.store(bottom + 1, std::memory_order_relaxed);
      }
    } else {
      _bottom.store(bottom + 1, std::memory_order_relaxed);
    }
    return item;
  }

  // Any thread. Null on failure, which does not mean the queue was empty.
  [[nodiscard]] T steal() {
    std::int64_t top = _top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (const std::int64_t bottom = _bottom.load(std::memory_order_acquire);
        top >= bottom)
      return nullptr;

    Array* const array = _array.load(std::memory_order_acquire);
    T item = array->get(top);
    if (!_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed))
      return nullptr;
    return item;
  }

 private:
  struct Array {
    std::int64_t capacity;
    std::int64_t mask;
    std::atomic<T>* slots;

    explicit Array(std::int64_t size)
        : capacity(size),
          mask(size - 1),
          slots(new std::atomic<T>[static_cast<std::size_t>(size)]) {}
    ~Array() { delete[] slots; }
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    void put(std::int64_t at, T item) noexcept {
      slots[at & mask].store(item, std::memory_order_relaxed);
    }
    T get(std::int64_t at) noexcept {
      return slots[at & mask].load(std::memory_order_relaxed);
    }
  };

  Array* resize(Array* array, std::int64_t bottom, std::int64_t top) {
    auto grown = std::make_unique<Array>(2 * array->capacity);
    for (std::int64_t at = top; at != bottom; ++at)
      grown->put(at, array->get(at));
    _garbage.push_back(array);
    Array* const published = grown.release();
    _array.store(published, std::memory_order_release);
    return published;
  }

  // Doubling the alignment is what measures best: it keeps a thief's read of
  // `_top` off the owner's `_bottom` line and off its neighbour's.
  alignas(128) std::atomic<std::int64_t> _top{0};
  alignas(128) std::atomic<std::int64_t> _bottom{0};
  std::atomic<Array*> _array;
  std::vector<Array*> _garbage;  // owner only
};

}  // namespace cursiv
