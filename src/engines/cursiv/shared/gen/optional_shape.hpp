// How many optional positions distribute before auxiliaries bound the body.
#pragma once

namespace cursiv {

// An alternative holding no more than this distributes, one production per
// subset; surplus positions use nullable auxiliaries.
inline constexpr unsigned kOptionalGate = 7;

static_assert(kOptionalGate <= 15,
              "the optional expansion gate must not exceed fifteen: one "
              "alternative distributes 2^n productions and a production count "
              "is sixteen bits");

}  // namespace cursiv
