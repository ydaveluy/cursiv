#pragma once

#include <iosfwd>
#include <string_view>

namespace cursiv {
struct SharedCoreServices;
}

namespace arith {

// Builds the file, then prints what each `Evaluation` comes to on `out` -- one
// line per expression, with the source it came from -- and each one with no
// value on `err`. A document carrying an error is not evaluated: the answer
// would be about text the author did not write. Answers `kDiagnostics` when an
// evaluation has no value.
[[nodiscard]] int eval_file(std::string_view path,
                            cursiv::SharedCoreServices& shared,
                            std::ostream& out, std::ostream& err);

}  // namespace arith
