#pragma once

#include <string_view>

namespace cursiv {
struct SharedCoreServices;
}

namespace arith {

// Builds the file, then prints what each `Evaluation` comes to -- one line per
// expression, with the source it came from. A document carrying an error is not
// evaluated: the answer would be about text the author did not write.
[[nodiscard]] int eval_file(std::string_view path,
                            cursiv::SharedCoreServices& shared);

}  // namespace arith
