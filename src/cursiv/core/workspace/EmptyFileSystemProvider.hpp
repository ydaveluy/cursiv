#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/workspace/FileSystemProvider.hpp"

// A workspace with no disk under it: every read fails, nothing exists. What an
// embedder installs when documents only ever arrive as text.

namespace cursiv {

class EmptyFileSystemProvider final : public FileSystemProvider {
 public:
  [[nodiscard]] FileSystemNode stat(std::string_view uri) const override;
  [[nodiscard]] bool exists(std::string_view uri) const override;
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override;
  [[nodiscard]] std::string readFile(std::string_view uri) const override;
  [[nodiscard]] std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const override;
};

}  // namespace cursiv
