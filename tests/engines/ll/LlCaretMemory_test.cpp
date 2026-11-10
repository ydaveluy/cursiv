// A caret keeps nothing: what it worked in goes with the answer, however deep
// the document it read.

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/Check.hpp"
#include "repairgrowthll/generated/Ll/Module.hpp"

namespace {
std::atomic<std::size_t> live{0};
}  // namespace

// Each block stores its size in front of it, because `delete` gets no size.
void* operator new(std::size_t size) {
  constexpr std::size_t room = alignof(std::max_align_t);
  void* const block = std::malloc(size + room);
  if (block == nullptr) throw std::bad_alloc();
  *static_cast<std::size_t*>(block) = size;
  live.fetch_add(size, std::memory_order_relaxed);
  return static_cast<char*>(block) + room;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
  if (p == nullptr) return;
  constexpr std::size_t room = alignof(std::max_align_t);
  void* const block = static_cast<char*>(p) - room;
  live.fetch_sub(*static_cast<std::size_t*>(block), std::memory_order_relaxed);
  std::free(block);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

int main() {
  const auto parser = tllrepairgrowth::generated::Ll::create_parser();
  // Memory a parse keeps on the thread for the next parse is not the caret's.
  const auto kept_by_caret = [&](const std::string& text) {
    (void)parser->parse(text);
    const std::size_t before = live.load(std::memory_order_relaxed);
    {
      const cursiv::ParseResult parsed = parser->parse(text);
      std::vector<cursiv::NextFeature> next;
      const auto at = static_cast<cursiv::TextOffset>(text.size());
      parser->nextFeaturesAt(parsed.cst(), at, at, next);
      CHECK(!next.empty());
    }
    return live.load(std::memory_order_relaxed) - before;
  };
  std::string deep = "nested:";
  for (unsigned i = 0; i < 1024; ++i) deep += " a";
  // The one-time setup of the first query is not counted.
  (void)kept_by_caret("nested: a");
  const std::size_t kept = kept_by_caret(deep);
  CHECK(kept == 0);
  if (kept != 0) std::printf("a caret kept %zu bytes\n", kept);
  return cursiv::test::report("LlCaretMemory_test");
}
