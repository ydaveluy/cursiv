// The program once it is checked and its stamps are planned: what the
// emitters read, and all they read.
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlStampPlan.hpp"

namespace cursiv {

// A count the tables address with sixteen bits: true where `n` fits. Where it
// does not, the sentence names which count and what it holds -- an author who
// hits this splits the grammar.
bool ll_fits(std::string_view grammar, std::size_t n, const char* what);

// The built program, checked and planned, held read-only: no write reaches a
// row an emitter reads. It is the program that was handed in, not a copy.
class LlFinalProgram {
 public:
  // Refuses, in this order: a fault the build recorded, what the lowering
  // refused or a limit it ran into, a record stamp that fits no split, and a
  // terminal that matches the empty string. A refusal goes to stderr under
  // `grammar`, and nothing is returned. Otherwise the stamp split is kept,
  // the program is frozen and its stamps are planned. `asWritten` answers a
  // rule as its author wrote it, shown under a fault; none where null.
  static std::optional<LlFinalProgram> make(
      LlBuiltProgram built, std::string_view grammar,
      std::string (*asWritten)(std::string_view) = nullptr);

  const LlBuiltProgram& program() const { return *_program; }
  const LlStampPlan& plan() const { return _plan; }

 private:
  explicit LlFinalProgram(std::unique_ptr<const LlBuiltProgram> built)
      : _program(std::move(built)), _plan(*_program) {}

  std::unique_ptr<const LlBuiltProgram> _program;
  LlStampPlan _plan;
};

}  // namespace cursiv
