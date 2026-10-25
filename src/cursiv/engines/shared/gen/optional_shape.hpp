// How many optional positions distribute before auxiliaries bound the body.
#pragma once

namespace cursiv {

// An alternative holding no more than this distributes, one production per
// subset; surplus positions use nullable auxiliaries.
#ifndef CURSIV_OPTIONAL_GATE
#define CURSIV_OPTIONAL_GATE 7
#endif
inline constexpr unsigned kOptionalGate = CURSIV_OPTIONAL_GATE;

static_assert(kOptionalGate <= 15,
              "CURSIV_OPTIONAL_GATE must not exceed fifteen: one "
              "alternative distributes 2^n productions and a production count "
              "is sixteen bits");

}  // namespace cursiv
