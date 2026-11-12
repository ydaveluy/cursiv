// A parse makes and drops its context once: its members are built and freed
// here, not in every language.
#include "cursiv/lr/runtime/LrContext.hpp"

#include <string_view>

#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

LrContext::LrContext(std::string_view text, CstBuilder& cstBuilder)
    : tape(cstBuilder.stream()),
      src(text.data()),
      len(static_cast<TextOffset>(text.size())),
      reads(len) {
  stack.reserve(64);
}

LrContext::~LrContext() = default;

}  // namespace cursiv::engine
