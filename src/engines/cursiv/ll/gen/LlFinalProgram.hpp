// The program once it is checked and its stamps are planned: what the
// emitters read, and all they read.
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/ll/gen/LlStampPlan.hpp"

namespace cursiv::engine {

// The built program, checked and planned, held read-only: no write reaches a
// row an emitter reads. It is the program that was handed in, not a copy.
class LlFinalProgram {
 public:
  // Refuses, in order: a build fault, a lowering refusal or limit, a stamp that
  // fits no split, and a terminal matching the empty string, printed to stderr
  // under `grammar`. `asWritten`, if not null, shows a faulty rule as written.
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

}  // namespace cursiv::engine
