#include "cursiv/VirtualFileSystemProvider.hpp"

#include <algorithm>
#include <utility>

namespace cursiv::test {

void VirtualFileSystemProvider::insert(std::string_view uri,
                                       std::string content) {
  addFile(uri, std::move(content));
}

std::vector<FileSystemNode> VirtualFileSystemProvider::readDirectory(
    std::string_view uri) const {
  if (!stat(uri).isDirectory)
    throw FileSystemError("cursiv: no directory at '" + std::string(uri) + "'");
  auto nodes = MemoryFileSystemProvider::readDirectory(uri);
  std::ranges::sort(nodes, {}, &FileSystemNode::uri);
  return nodes;
}

}  // namespace cursiv::test
