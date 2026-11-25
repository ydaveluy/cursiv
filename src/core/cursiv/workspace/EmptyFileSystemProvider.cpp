#include "cursiv/workspace/EmptyFileSystemProvider.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/workspace/FileSystemProvider.hpp"

namespace cursiv {

FileSystemNode EmptyFileSystemProvider::stat(std::string_view uri) const {
  throw FileSystemError("cursiv: no file system, cannot stat '" +
                        std::string(uri) + "'");
}

bool EmptyFileSystemProvider::exists(std::string_view) const { return false; }

std::vector<std::uint8_t> EmptyFileSystemProvider::readBinary(
    std::string_view uri) const {
  throw FileSystemError("cursiv: no file system, cannot read '" +
                        std::string(uri) + "'");
}

std::string EmptyFileSystemProvider::readFile(std::string_view uri) const {
  throw FileSystemError("cursiv: no file system, cannot read '" +
                        std::string(uri) + "'");
}

// Not an error: an empty workspace has directories with nothing in them.
std::vector<FileSystemNode> EmptyFileSystemProvider::readDirectory(
    std::string_view) const {
  return {};
}

}  // namespace cursiv
