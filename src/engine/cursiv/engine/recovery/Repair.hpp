// What a parse does with a token its state refuses: the cheapest edits of
// the text that let it read on are searched, then written as the states are
// driven again.
#pragma once

#include "cursiv/text/TextOffset.hpp"

namespace cursiv {
class CstBuilder;
}

namespace cursiv::engine {

struct Machine;

// Reads on from the token `machine` was refused to the end of its text, and
// notes each fault repaired on `builder`. False where no repair reads the
// text through. With `whole`, the machine's text ends at a caret and goes on
// up to `whole`: a search reads all of it, and no edit is made past the caret.
bool repair(Machine& machine, CstBuilder& builder, const char* whole = nullptr);

}  // namespace cursiv::engine
