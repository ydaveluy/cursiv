// A language's states written out as C++: each state is a label, its mode's
// tokens are read by constants, and a transition is a jump.
#pragma once

#include <string>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Pack.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv::engine::gen {

// Writes the shared state definitions and one entry unit per machine.
using WriteSource = FunctionRef<void(const char*, const std::string&)>;
struct LanguageNames;
void emit_drive(const Cfg& cfg, const Packed& packed, const LanguageNames& names, WriteSource write);

}  // namespace cursiv::engine::gen
