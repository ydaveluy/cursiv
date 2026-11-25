// A language's parser written out as C++: its tables, its lexers and tests,
// and the steps its machines take over the tables.
#pragma once

#include <initializer_list>
#include <string_view>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Pack.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv::engine::gen {

// Writes one generated file: its name, and its text as parts in order.
using WriteSource = FunctionRef<void(const char*, std::initializer_list<std::string_view>)>;
struct LanguageNames;
void emit_drive(const Cfg& cfg, const Packed& packed, const LanguageNames& names, WriteSource write);

}  // namespace cursiv::engine::gen
