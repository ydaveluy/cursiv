#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/workspace/FileSystemProvider.hpp"

// The machine's own disk, reached through `file:` URIs. A URI that names no
// local path is a read that fails, never a path guessed from it.

namespace cursiv {

class LocalFileSystemProvider final : public FileSystemProvider {
 public:
  LocalFileSystemProvider() = default;
  // Bounds text reads before allocation and as the file grows.
  explicit LocalFileSystemProvider(std::size_t maxTextBytes)
      : _maxTextBytes(maxTextBytes) {}

  [[nodiscard]] FileSystemNode stat(std::string_view uri) const override;
  [[nodiscard]] bool exists(std::string_view uri) const override;
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override;
  [[nodiscard]] std::string readFile(std::string_view uri) const override;
  [[nodiscard]] std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const override;

 private:
  std::size_t _maxTextBytes = std::numeric_limits<std::size_t>::max();
};

}  // namespace cursiv
