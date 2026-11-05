// Pins what an allocation refused part-way leaves behind: a publication, a
// queue, a pool's submission, a registration.
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <stop_token>
#include <string>
#include <thread>

#include "cursiv/Check.hpp"
#include "cursiv/execution/TaskQueue.hpp"
#include "cursiv/execution/WorkerPool.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/AstConvThunks.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/Published.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace {
std::atomic<int> failAfter{-1};
std::atomic<std::thread::id> failThread;
std::atomic<int> allocations{0};
std::atomic<int> stopAfter{-1};
std::stop_source* stopper = nullptr;
void fail_allocation(int after) {
  failThread = std::this_thread::get_id();
  failAfter.store(after, std::memory_order_release);
}
}  // namespace

// One controlled allocation failure on the submitting thread. Other workers
// can finish normally, which is the lifetime guarantee under test.
void* operator new(std::size_t size) {
  if (failAfter.load(std::memory_order_acquire) >= 0 &&
      failThread.load(std::memory_order_relaxed) ==
          std::this_thread::get_id() &&
      failAfter.fetch_sub(1, std::memory_order_relaxed) == 0)
    throw std::bad_alloc();
  if (stopAfter.load(std::memory_order_relaxed) >= 0 &&
      stopAfter.fetch_sub(1, std::memory_order_relaxed) == 0)
    stopper->request_stop();
  if (void* p = std::malloc(size == 0 ? 1 : size)) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    return p;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
  if (p != nullptr) {
    allocations.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
  }
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
struct RefusingResource : std::pmr::memory_resource {
  std::size_t requested = 0;
  void* do_allocate(std::size_t bytes, std::size_t) override {
    requested = bytes;
    throw std::bad_alloc();
  }
  void do_deallocate(void*, std::size_t, std::size_t) override {}
  bool do_is_equal(const memory_resource& other) const noexcept override {
    return this == &other;
  }
};

void reservation_sizes_do_not_wrap() {
  RefusingResource memory;
  cursiv::CstRun run;
  run.bind(&memory);
  bool threw = false;
  try {
    run.reserve_for(std::numeric_limits<std::size_t>::max());
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  CHECK(threw && memory.requested > 64 * sizeof(cursiv::CstNode));
}

// Every allocation of a parse refused in turn: what it built is gone, and a
// sanitizer run says nothing died twice.
void a_refused_allocation_frees_what_the_parse_built() {
  const auto parser = ws::generated::Workspace::create_parser();
  std::string text = "datatype D\n";
  for (int i = 0; i < 40; ++i) text += "entity E { many a : D b : D }\n";
  (void)parser->parse(text);
  int at = 0;
  for (;; ++at) {
    const int before = allocations.load(std::memory_order_relaxed);
    fail_allocation(at);
    bool refused = false;
    try {
      const cursiv::ParseResult parsed = parser->parse(text);
      CHECK(static_cast<bool>(parsed));
    } catch (const std::bad_alloc&) {
      refused = true;
    }
    failAfter.store(-1, std::memory_order_release);
    CHECK(allocations.load(std::memory_order_relaxed) == before);
    if (!refused) break;
  }
  CHECK(at > 0);
}

// A stop asked at many points of a large parse, the tree's build included.
void a_stopped_parse_frees_what_it_built() {
  const auto parser = ws::generated::Workspace::create_parser();
  std::string text = "datatype D\n";
  for (int i = 0; i < 5000; ++i) text += "entity E { many a : D b : D }\n";
  const int idle = allocations.load(std::memory_order_relaxed);
  {
    const cursiv::ParseResult parsed = parser->parse(text);
    CHECK(static_cast<bool>(parsed) && !parsed.nodes().empty());
  }
  CHECK(allocations.load(std::memory_order_relaxed) == idle);
  int stopped = 0;
  for (int point = 0; point < 40; ++point) {
    std::stop_source source;
    stopper = &source;
    stopAfter.store(point * 125, std::memory_order_relaxed);
    const int before = allocations.load(std::memory_order_relaxed);
    try {
      const cursiv::ParseResult parsed =
          parser->parse(text, source.get_token());
    } catch (const cursiv::OperationCancelled&) {
      ++stopped;
    }
    stopAfter.store(-1, std::memory_order_relaxed);
    stopper = nullptr;
    CHECK(allocations.load(std::memory_order_relaxed) == before);
  }
  CHECK(stopped != 0);
}

void a_failed_retirement_keeps_the_published_value_alive() {
  cursiv::Published<int> published;
  auto first = std::make_shared<const int>(1);
  const std::weak_ptr<const int> life = first;
  published.publish(std::move(first));
  auto second = std::make_shared<const int>(2);
  bool threw = false;
  fail_allocation(0);
  try {
    published.publish(second);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  failAfter = -1;
  CHECK(threw);
  CHECK(!life.expired());
  if (!life.expired()) CHECK(*published.read() == 1);
  published.publish(second);
  CHECK(*published.read() == 2);
}

void a_queue_allocation_failure_leaves_no_leak_or_lost_item() {
  for (int fail = 0; fail < 3; ++fail) {
    const int before = allocations.load();
    bool threw = false;
    fail_allocation(fail);
    try {
      cursiv::TaskQueue<int*> queue(0);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    failAfter = -1;
    CHECK(threw);
    CHECK(allocations.load() == before);
  }
  for (int fail = 0; fail < 2; ++fail) {
    cursiv::TaskQueue<int*> queue(0);
    int a = 1, b = 2;
    queue.push(&a);
    const int before = allocations.load();
    bool threw = false;
    fail_allocation(fail);
    try {
      queue.push(&b);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    failAfter = -1;
    CHECK(threw);
    CHECK(allocations.load() == before);
    CHECK(queue.steal() == &a);
    CHECK(queue.empty());
    queue.push(&b);
    CHECK(queue.pop() == &b);
  }
}

void a_partial_submission_joins_before_unwinding() {
  cursiv::WorkerPool pool(17);
  std::barrier entered(18);
  std::latch release(1);
  std::atomic<bool> elected{false};
  bool caught = false;
  // Other workers are occupied, so the nested submissions pile up in one
  // worker's queue until it stands four slots from full; the next submission
  // fails after publishing part of its jobs.
  const auto nested = [&](auto&& self, unsigned depth) -> void {
    if (depth == 60) fail_allocation(0);
    pool.run(18, [&](std::size_t i) noexcept {
      if (i == 0 && depth < 60) {
        try {
          self(self, depth + 1);
        } catch (const std::bad_alloc&) {
          caught = true;
        }
      }
    });
  };
  pool.run(18, [&](std::size_t) noexcept {
    entered.arrive_and_wait();
    if (pool.onWorkerThread() && !elected.exchange(true)) {
      nested(nested, 0);
      failAfter = -1;
      release.count_down();
    } else {
      release.wait();
    }
  });
  CHECK(caught);
  std::atomic<unsigned> calls{0};
  pool.run(100, [&](std::size_t) noexcept { ++calls; });
  CHECK(calls == 100);
}

struct Tracked final : cursiv::CoreServices {
  bool& destroyed;
  Tracked(cursiv::SharedCoreServices& shared, bool& flag)
      : CoreServices(shared), destroyed(flag) {}
  ~Tracked() noexcept override { destroyed = true; }
};

// Every allocation of one registration refused in turn, into a registry
// that already routes the extension -- so the collision's message is one of
// them. Whatever the step, a route that names the container names a live one.
void a_refused_allocation_leaves_no_route_to_a_freed_container() {
  cursiv::SharedCoreServices shared;
  cursiv::install_default_shared_core_services(shared);
  bool finished = false;
  for (int step = 0; step < 1000 && !finished; ++step) {
    // Outlives the registry, which destroys what writes it.
    bool destroyed = false;
    cursiv::DefaultServiceRegistry registry(shared);
    auto first = std::make_unique<cursiv::CoreServices>(shared);
    first->languageMetaData = {"first", {".q"}, {}};
    first->parser = ws::generated::Workspace::create_parser();
    first->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*first);
    registry.registerServices(std::move(first));

    auto tracked = std::make_unique<Tracked>(shared, destroyed);
    tracked->languageMetaData = {"tracked", {".q"}, {}};
    tracked->parser = ws::generated::Workspace::create_parser();
    tracked->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*tracked);
    const cursiv::CoreServices* const address = tracked.get();

    bool refused = false;
    fail_allocation(step);
    try {
      registry.registerServices(std::move(tracked));
    } catch (const std::bad_alloc&) {
      refused = true;
    }
    failAfter.store(-1, std::memory_order_release);
    finished = !refused;
    if (destroyed) {
      CHECK(registry.findServices("file:///a.x", "tracked") != address);
      CHECK(registry.findServices("file:///a.q") != address);
    }
  }
  CHECK(finished);
}
}  // namespace

// Parts the source does not hold side by side are written once, where they
// stay: with room in the pool, a join asks for no memory of its own.
void a_join_writes_its_parts_where_they_stay() {
  const std::string left(2048, 'a');
  const std::string right(2048, 'b');
  cursiv::Cst tree("x");
  cursiv::AstBuildCtx ctx;
  ctx.cst = &tree;
  (void)ctx.keep_text(std::string_view("x"));  // the pool's first block
  cursiv::detail::PartRun run;
  run.push_back(left);
  run.push_back(right);
  std::string_view joined;
  bool allocated = false;
  fail_allocation(0);
  try {
    joined = cursiv::detail::join_parts(ctx, run);
  } catch (const std::bad_alloc&) {
    allocated = true;
  }
  failAfter.store(-1, std::memory_order_release);
  CHECK(!allocated);
  CHECK(joined == left + right);
}

int main() {
  reservation_sizes_do_not_wrap();
  a_join_writes_its_parts_where_they_stay();
  a_refused_allocation_frees_what_the_parse_built();
  a_stopped_parse_frees_what_it_built();
  a_failed_retirement_keeps_the_published_value_alive();
  a_queue_allocation_failure_leaves_no_leak_or_lost_item();
  a_partial_submission_joins_before_unwinding();
  a_refused_allocation_leaves_no_route_to_a_freed_container();
  return cursiv::test::report("AllocationFailure_test");
}
