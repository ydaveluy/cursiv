#include <cstdlib>

#include "cursiv/core/workspace/TextDocument.hpp"

#ifdef EXPECT_MIMALLOC
#include <mimalloc.h>
#endif

int main() {
  const auto document =
      cursiv::TextDocument::create("file:///test", "test", 1, "hello\n");
#ifdef EXPECT_MIMALLOC
  void* allocation = std::malloc(128);
  const bool owned = mi_is_in_heap_region(allocation);
  std::free(allocation);
  if (!owned) return 1;
#endif
  return document.lineCount() == 2 ? 0 : 1;
}
