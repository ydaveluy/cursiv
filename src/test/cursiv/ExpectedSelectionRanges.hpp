#pragma once

#include "cursiv/Disposable.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {
struct LspServices;
}

namespace cursiv::test {

// One caret and one marked range per step, including the outermost selection.
[[nodiscard]] Disposable expectSelectionRanges(
    const LspServices& services, const ExpectedBase& input,
    const CancellationToken& cancel = {});

}  // namespace cursiv::test
