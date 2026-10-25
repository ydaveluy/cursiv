// Exercises the WASI host's thread creation and lifetime through pthreads.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <latch>
#include <mutex>
#include <string_view>
#include <thread>

#include "cursiv/test/Check.hpp"

namespace {
using namespace std::chrono_literals;
thread_local unsigned localValue = 0;

void concurrent_nested_threads_keep_their_own_identity() {
  std::recursive_mutex mutex;
  std::atomic<bool> valid{true}, inside{false}, begin{false};
  std::latch ready{4};
  std::array<std::thread, 2> parents;
  for (unsigned p = 0; p < parents.size(); ++p) {
    parents[p] = std::thread([&, p] {
      localValue = 100 + p;
      std::array<std::thread, 2> children;
      for (unsigned c = 0; c < children.size(); ++c) {
        children[c] = std::thread([&, p, c] {
          if (localValue != 0) valid = false;
          localValue = 1 + 2 * p + c;
          ready.count_down();
          begin.wait(false);
          for (unsigned i = 0; i < 8; ++i) {
            const std::lock_guard outer(mutex);
            const std::lock_guard inner(mutex);
            // Colliding WASI IDs can make a recursive mutex mistake another
            // thread for its owner. Atomics make that failure safe to observe.
            if (inside.exchange(true)) valid = false;
            std::this_thread::sleep_for(1ms);
            if (localValue != 1 + 2 * p + c) valid = false;
            inside = false;
          }
        });
      }
      for (auto& child : children) child.join();
      if (localValue != 100 + p) valid = false;
    });
  }
  ready.wait();
  begin = true;
  begin.notify_all();
  for (auto& parent : parents) parent.join();
  CHECK(valid);
  CHECK(localValue == 0);
}

void a_child_outlives_its_creator_and_can_spawn_again() {
  std::atomic<bool> begin{false};
  unsigned value = 0;
  std::thread child;
  std::thread parent([&] {
    child = std::thread([&] {
      begin.wait(false);
      std::thread grandchild([&] { value = 42; });
      grandchild.join();
    });
  });
  parent.join();
  // Let the parent's JS event loop finish after its C++ entry point returns.
  // Its child must keep that host alive while waiting for this release.
  std::this_thread::sleep_for(100ms);
  begin = true;
  begin.notify_one();
  child.join();
  CHECK(value == 42);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view mode(argv[1]);
    if (mode == "worker-exit7" || mode == "worker-trap") {
      std::thread parent([&] {
        std::thread child([&] {
          if (mode == "worker-exit7") std::exit(7);
          __builtin_trap();
        });
        child.join();
      });
      parent.join();
      return 0;  // Returning would mean the fatal worker was lost.
    }
  }
  concurrent_nested_threads_keep_their_own_identity();
  a_child_outlives_its_creator_and_can_spawn_again();
  return cursiv::test::report("WasiThreads_test");
}
