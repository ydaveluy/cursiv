#pragma once

#include <optional>
#include <string>
#include <string_view>

// Where a generated file goes and what it is called. The rest of what a
// command line needs -- scanning a folder, building one document, printing
// the verdict -- is `cursiv/cli/cli.hpp`'s, and is not restated here.

namespace dm {

struct FilePathData {
  std::string destination;
  std::string name;
};

// `destination` defaults to `generated`; the name is the input's stem with the
// characters a file name may not carry removed.
[[nodiscard]] FilePathData extract_destination_and_name(
    std::string_view filePath, std::optional<std::string_view> destination);

}  // namespace dm
