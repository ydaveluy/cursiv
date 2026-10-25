#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace cursiv {

FileSystemNode EmptyFileSystemProvider::stat(std::string_view uri) const {
  throw FileSystemError(
      std::format("cursiv: no file system, cannot stat '{}'", uri));
}

bool EmptyFileSystemProvider::exists(std::string_view) const { return false; }

std::vector<std::uint8_t> EmptyFileSystemProvider::readBinary(
    std::string_view uri) const {
  throw FileSystemError(
      std::format("cursiv: no file system, cannot read '{}'", uri));
}

std::string EmptyFileSystemProvider::readFile(std::string_view uri) const {
  throw FileSystemError(
      std::format("cursiv: no file system, cannot read '{}'", uri));
}

// Not an error: an empty workspace has directories with nothing in them.
std::vector<FileSystemNode> EmptyFileSystemProvider::readDirectory(
    std::string_view) const {
  return {};
}

}  // namespace cursiv
