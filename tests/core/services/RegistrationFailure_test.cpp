// Pins what a registration that fails part-way leaves: an allocation refused
// at each step in turn, and no route may name a container the registry let go.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/DefaultServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace {
std::atomic<int> failAfter{-1};
std::atomic<std::thread::id> failThread;
void fail_allocation(int after) {
  failThread = std::this_thread::get_id();
  failAfter.store(after, std::memory_order_release);
}
}  // namespace

void* operator new(std::size_t size) {
  if (failAfter.load(std::memory_order_acquire) >= 0 &&
      failThread.load(std::memory_order_relaxed) ==
          std::this_thread::get_id() &&
      failAfter.fetch_sub(1, std::memory_order_relaxed) == 0)
    throw std::bad_alloc();
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

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
    cursiv::DefaultServiceRegistry registry(shared);
    auto first = std::make_unique<cursiv::CoreServices>(shared);
    first->languageMetaData = {"first", {".q"}, {}};
    first->parser = ws::generated::Workspace::create_parser();
    first->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*first);
    registry.registerServices(std::move(first));

    bool destroyed = false;
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

int main() {
  a_refused_allocation_leaves_no_route_to_a_freed_container();
  return cursiv::test::report("registration_failure");
}
