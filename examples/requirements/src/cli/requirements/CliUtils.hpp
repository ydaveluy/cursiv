#pragma once

#include <optional>
#include <string>
#include <string_view>

// Where a generated file goes and what it is called. The rest of what a
// command line needs is in `cursiv/cli.hpp`.

namespace req {

struct FilePathData {
  std::string destination;
  std::string name;
};

// `destination` defaults to `generated`. The name is the input's stem without
// dots and dashes, or `generated` if nothing is left: two inputs can share it.
[[nodiscard]] FilePathData extract_destination_and_name(
    std::string_view filePath, std::optional<std::string_view> destination);

}  // namespace req
