#pragma once

#include <iosfwd>
#include <string_view>

namespace cursiv {
struct SharedCoreServices;
}

namespace arith {

// Builds the file and prints each `Evaluation` with its value on `out`, or on
// `err` when it has none, then answers `kDiagnostics`. A document with an
// error is not evaluated.
[[nodiscard]] int eval_file(std::string_view path,
                            cursiv::SharedCoreServices& shared,
                            std::ostream& out, std::ostream& err);

}  // namespace arith
