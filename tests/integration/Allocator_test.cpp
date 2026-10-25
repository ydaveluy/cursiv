// Every cursiv executable has one allocator, libc++ and pthreads included.
// Exercises the thread teardown and frees performed by a different thread;
// the linear-memory check is the wasm port's.
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <mimalloc.h>
#include <mimalloc-stats.h>

#include "cursiv/test/Check.hpp"

namespace {
void standard_allocations_use_the_module_allocator() {
  auto* bytes = static_cast<unsigned char*>(std::calloc(128, 1));
  CHECK(bytes != nullptr);
  if (bytes == nullptr) return;
  CHECK(mi_is_in_heap_region(bytes));
  for (unsigned i = 0; i < 128; ++i) CHECK(bytes[i] == 0);
  std::memset(bytes, 42, 128);
  auto* grown = static_cast<unsigned char*>(std::realloc(bytes, 8192));
  CHECK(grown != nullptr);
  if (grown == nullptr) {
    std::free(bytes);
    return;
  }
  CHECK(mi_is_in_heap_region(grown));
  for (unsigned i = 0; i < 128; ++i) CHECK(grown[i] == 42);
  std::free(grown);

  const std::vector<int> values(4096, 42);
  const std::string text(4096, 'a');
  CHECK(mi_is_in_heap_region(values.data()));
  CHECK(mi_is_in_heap_region(text.data()));
  struct alignas(256) Aligned { std::array<char, 512> bytes{}; };
  const auto aligned = std::make_unique<Aligned>();
  CHECK(mi_is_in_heap_region(aligned.get()));
  CHECK(reinterpret_cast<std::uintptr_t>(aligned.get()) % alignof(Aligned) == 0);
  void* block = nullptr;
  CHECK(posix_memalign(&block, 256, 1024) == 0);
  CHECK(block != nullptr);
  if (block == nullptr) return;
  CHECK(mi_is_in_heap_region(block));
  CHECK(reinterpret_cast<std::uintptr_t>(block) % 256 == 0);
  std::free(block);

  // A tiny object can occupy a larger size class because of its alignment.
  for (std::size_t alignment : {256, 4096, 65536}) {
    void* tiny = mi_malloc_aligned(1, alignment);
    CHECK(tiny != nullptr);
    if (tiny == nullptr) continue;
    CHECK(reinterpret_cast<std::uintptr_t>(tiny) % alignment == 0);
    mi_free_size_aligned(tiny, 1, alignment);
  }
}

std::int64_t allocator_threads() {
  mi_stats_t stats;
  mi_stats_init(&stats);
  CHECK(mi_stats_get(&stats));
  return stats.threads.current;
}

#ifdef __wasi__
void large_allocations_reuse_linear_memory() {
  // WASM cannot shrink memory. Discarding large pages on free would exhaust
  // the module even though only one block is alive at a time.
  std::size_t warmedPages = 0;
  for (unsigned i = 0; i < 128; ++i) {
    auto* block = static_cast<unsigned char*>(std::malloc(4 * 1024 * 1024));
    CHECK(block != nullptr);
    if (block == nullptr) return;
    block[0] = 42;
    block[4 * 1024 * 1024 - 1] = 17;
    std::free(block);
    // libc++'s pools also request large alignments. Their blocks must take
    // the reusable arena path rather than disposable OS allocations.
    const std::size_t alignment = std::size_t{1} << (16 + i % 8);
    void* aligned = std::aligned_alloc(alignment, alignment);
    CHECK(aligned != nullptr);
    if (aligned == nullptr) return;
    CHECK(reinterpret_cast<std::uintptr_t>(aligned) % alignment == 0);
    std::free(aligned);
    if (i == 15) warmedPages = __builtin_wasm_memory_size(0);
  }
  CHECK(__builtin_wasm_memory_size(0) == warmedPages);
}
#endif

void cross_thread_frees_and_repeated_thread_exit() {
  constexpr unsigned workerCount = 8, blockCount = 64;
  const auto baseline = allocator_threads();
  for (unsigned round = 0; round < 12; ++round) {
    std::array<std::array<unsigned char*, blockCount>, workerCount> blocks{};
    std::array<std::thread, workerCount> workers;
    std::atomic<bool> valid{true};
    std::barrier allocated(workerCount);
    std::latch ready(workerCount), release(1);
    const auto size = [](unsigned i) { return 32 + 79 * i; };
    const auto checkAndFree = [&](unsigned owner, unsigned i) {
      auto* p = blocks[owner][i];
      if (p == nullptr) {
        valid = false;
        return;
      }
      for (unsigned j = 0; j < size(i); ++j)
        if (p[j] != static_cast<unsigned char>(owner + i + round)) valid = false;
      std::free(p);
    };
    for (unsigned w = 0; w < workerCount; ++w) {
      workers[w] = std::thread([&, w] {
        for (unsigned i = 0; i < blockCount; ++i) {
          auto* p = static_cast<unsigned char*>(std::malloc(size(i)));
          blocks[w][i] = p;
          if (p != nullptr) std::memset(p, static_cast<int>(w + i + round), size(i));
        }
        allocated.arrive_and_wait();
        // Free another live thread's allocations, then keep all owners alive
        // while the main thread checks that their heaps were registered.
        for (unsigned i = 0; i < blockCount / 2; ++i)
          checkAndFree((w + 1) % workerCount, i);
        ready.count_down();
        release.wait();
      });
    }
    ready.wait();
    CHECK(allocator_threads() == baseline + workerCount);
    release.count_down();
    for (auto& worker : workers) worker.join();
    // A pthread destructor must unregister the heap without invalidating its
    // surviving allocations. The next pool can reuse the abandoned storage.
    CHECK(allocator_threads() == baseline);
    for (unsigned w = 0; w < workerCount; ++w)
      for (unsigned i = blockCount / 2; i < blockCount; ++i) checkAndFree(w, i);
    CHECK(valid);
  }
}
}  // namespace

int main() {
  std::atomic<unsigned> errors{0};
  mi_register_error([](int, void* arg) {
    static_cast<std::atomic<unsigned>*>(arg)->fetch_add(1, std::memory_order_relaxed);
  }, &errors);
  standard_allocations_use_the_module_allocator();
#ifdef __wasi__
  large_allocations_reuse_linear_memory();
#endif
  cross_thread_frees_and_repeated_thread_exit();
  CHECK(errors == 0);
  mi_register_error(nullptr, nullptr);
  return cursiv::test::report("Allocator_test");
}
